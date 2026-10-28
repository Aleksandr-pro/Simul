// ============================================================================
// @simul/core/engine — Главный фасад ядра симуляционного движка Simul
// ============================================================================

import { AudiencePool } from './agents/audience-pool.js';
import { RecommendationEngine } from './algorithm/recommender.js';
import { SimulationClock } from './clock/timeline.js';
import { HybridCommentGenerator, type LLMCommentEnricher } from './comments/generator.js';
import { buildVideoPostMortem } from './content/diagnostics.js';
import {
  evaluatePackagingVariant,
  solveEditingMiniGame,
  solveStreamMiniGame,
  type PackagingEvaluation,
  type StreamMiniGameInput,
  type StreamMiniGameResult,
} from './content/minigames.js';
import { clamp } from './math/curves.js';
import { SeededPrng } from './math/prng.js';
import { DilemmaEngine } from './narrative/dilemmas.js';
import { TrendSystem } from './narrative/trends.js';
import {
  SIMUL_SNAPSHOT_VERSION,
  type SimulEngineSnapshot,
} from './serialization/snapshot.js';
import { CreatorAndTeamSystem } from './team/creator-team.js';
import {
  NICHE_SPECS,
  type AudienceSummary,
  type CreatorState,
  type DilemmaDefinition,
  type EditingMiniGameResult,
  type NicheId,
  type PublishedVideoRecord,
  type ScheduledConsequence,
  type SimulEvent,
  type TeamMember,
  type TeamRole,
  type TrendCard,
  type VideoReleaseInput,
  type VideoWavePoint,
} from './types.js';

export interface SimulEngineConfig {
  seed?: number;
  handle?: string;
  displayName?: string;
  originNiche?: NicheId;
  startingCashUsd?: number;
  audienceCapacity?: number;
  initialPlatformAgents?: number;
}

interface ActiveLiveSimulationContext {
  video: PublishedVideoRecord;
  input: VideoReleaseInput;
  editResult: EditingMiniGameResult;
  evalA: PackagingEvaluation;
  evalB: PackagingEvaluation | null;
  intrinsicQuality: number;
  trendBoost: number;
  consistencyMultiplier: number;
  publishHourMultiplier: number;
  collabBonusImpressions: number;
  adAnnoyance: number;
  sketchySponsorLevel: number;
  currentViralScore: number;
  variantAImpressions: number;
  variantAViews: number;
  variantBImpressions: number;
  variantBViews: number;
  lockedVariant: 'A' | 'B' | null;
}

export type SimulEventListener = (event: SimulEvent) => void;

export class SimulEngine {
  public readonly initialSeed: number;
  public readonly prng: SeededPrng;
  public readonly clock: SimulationClock;
  public readonly audience: AudiencePool;
  public readonly recommender: RecommendationEngine;
  public readonly trends: TrendSystem;
  public readonly dilemmas: DilemmaEngine;
  public readonly comments: HybridCommentGenerator;
  public readonly creatorSystem: CreatorAndTeamSystem;

  private videos: PublishedVideoRecord[] = [];
  private activeLiveContext: ActiveLiveSimulationContext | null = null;
  private worldPoolMultiplierValue = 1.0;
  private listeners = new Set<SimulEventListener>();

  constructor(config: SimulEngineConfig = {}) {
    this.initialSeed = (config.seed ?? 20261029) >>> 0;
    this.prng = new SeededPrng(this.initialSeed);
    this.clock = new SimulationClock(10); // День 1, 10:00
    const niche = config.originNiche ?? 'tech';

    this.audience = new AudiencePool(config.audienceCapacity ?? 20_000);
    this.audience.seedPlatformPopulation(
      this.prng,
      niche,
      config.initialPlatformAgents ?? 10_000
    );

    this.recommender = new RecommendationEngine();
    this.trends = new TrendSystem();
    this.trends.seedInitialTrends(this.prng, this.clock.currentDay);

    this.dilemmas = new DilemmaEngine();
    this.comments = new HybridCommentGenerator();
    this.creatorSystem = new CreatorAndTeamSystem({
      handle: config.handle ?? '@creator_zero',
      displayName: config.displayName ?? 'Новый Канал',
      niche,
      startingCashUsd: config.startingCashUsd ?? 500,
    });
  }

  // --- Подписка на события (реактивный мост с UI / рендерером / звуком) ---

  subscribe(listener: SimulEventListener): () => void {
    this.listeners.add(listener);
    return () => {
      this.listeners.delete(listener);
    };
  }

  private emit(event: SimulEvent): void {
    for (const listener of this.listeners) {
      listener(event);
    }
  }

  // --- Доступ к состоянию ---

  get creator(): CreatorState {
    return this.creatorSystem.creator;
  }

  get stage() {
    return this.creatorSystem.currentStage;
  }

  get stageDefinition() {
    return this.creatorSystem.stageDefinition;
  }

  get subscribers(): number {
    return this.audience.subscribers;
  }

  get totalViews(): number {
    let sum = 0;
    for (const v of this.videos) sum += v.totalViews;
    return sum;
  }

  get publishedVideos(): readonly PublishedVideoRecord[] {
    return this.videos;
  }

  get isLiveVideoRunning(): boolean {
    return this.activeLiveContext !== null;
  }

  get worldPoolMultiplier(): number {
    return this.worldPoolMultiplierValue;
  }

  /** Обновляется сетевым адаптером (@simul/cloud) из общего пула ниши */
  setWorldPoolMultiplier(multiplier: number): void {
    this.worldPoolMultiplierValue = clamp(multiplier, 0.5, 1.4);
  }

  setLLMEnricher(enricher: LLMCommentEnricher | null): void {
    this.comments.setLLMEnricher(enricher);
  }

  getAudienceSummary(): AudienceSummary {
    return this.audience.summarize();
  }

  getActiveTrends(): readonly TrendCard[] {
    return this.trends.getActiveTrends();
  }

  getAvailableDilemmas(): DilemmaDefinition[] {
    return this.dilemmas.getAvailableDilemmas(this.stage);
  }

  // --- Начало релиза видео (запускает живую 48-часовую симуляцию волн) ---

  beginVideoRelease(input: VideoReleaseInput): PublishedVideoRecord {
    if (this.activeLiveContext !== null) {
      throw new Error('Дождитесь завершения первых 48 часов текущего релиза');
    }

    const budget = Math.max(0, input.productionBudgetUsd);
    if (this.creator.cashUsd < budget) {
      throw new Error(
        `Недостаточно бюджета на съёмку: нужно $${budget}, доступно $${this.creator.cashUsd.toFixed(0)}`
      );
    }

    // Выравниваем часы до выбранного часа публикации в текущих или следующих сутках
    const targetHour = ((input.publishHourOfDay % 24) + 24) % 24;
    const currentH = this.clock.hourOfDay;
    const hoursUntilPublish =
      targetHour >= currentH ? targetHour - currentH : 24 - currentH + targetHour;
    if (hoursUntilPublish > 0) {
      this.advanceHoursInternal(hoursUntilPublish);
    }

    this.creator.cashUsd -= budget;
    this.creator.currentNiche = input.niche;

    const skills = this.creatorSystem.getEffectiveSkills();
    const editResult = solveEditingMiniGame(
      input.editing,
      skills,
      this.creator.equipmentTier,
      this.creator.burnout
    );

    const evalA = evaluatePackagingVariant(
      input.packaging.variantA,
      input.niche,
      skills.analytics
    );
    const evalB = input.packaging.variantB
      ? evaluatePackagingVariant(
          input.packaging.variantB,
          input.niche,
          skills.analytics
        )
      : null;

    // Расчёт истинного качества контента (сценарий + монтаж + бюджет + харизма + оборудование)
    const budgetFactor = clamp(Math.log10(1 + budget / 80) * 0.14, 0, 0.22);
    const equipFactor = ((this.creator.equipmentTier - 1) / 4) * 0.14;
    const skillFactor =
      (skills.scriptwriting * 0.4 + skills.charisma * 0.35 + skills.editing * 0.25) /
      100;
    const burnoutPenalty =
      this.creator.burnout > 70 ? ((this.creator.burnout - 70) / 30) * 0.20 : 0;

    const intrinsicQuality = clamp(
      editResult.overallEditScore * 0.48 +
        skillFactor * 0.34 +
        budgetFactor +
        equipFactor -
        burnoutPenalty,
      0.08,
      1.0
    );

    // Проверка попадания в активный тренд
    const matchedTrend = this.trends.findById(input.trendId);
    const trendBoost =
      matchedTrend && matchedTrend.niche === input.niche
        ? matchedTrend.viralityBoost
        : matchedTrend
          ? matchedTrend.viralityBoost * 0.88
          : 1.0;

    const consistencyMultiplier = this.recommender.registerUploadOnDay(
      this.clock.currentDay
    );
    const publishHourMultiplier = this.recommender.computePublishHourMultiplier(
      this.clock.hourOfDay
    );

    const sponsorPayout = input.sponsorDeal ? Math.max(0, input.sponsorDeal.payoutUsd) : 0;
    const sketchySponsorLevel = input.sponsorDeal
      ? clamp(input.sponsorDeal.sketchiness, 0, 1)
      : 0;
    const adAnnoyance = input.sponsorDeal
      ? clamp(0.18 + sketchySponsorLevel * 0.65, 0, 1)
      : 0;

    if (sponsorPayout > 0) {
      this.creator.cashUsd += sponsorPayout;
      if (sketchySponsorLevel > 0.45) {
        const banChanged = this.recommender.adjustTrust(
          -sketchySponsorLevel * 0.14
        );
        if (banChanged) {
          this.emit({
            type: 'shadowban:changed',
            active: this.recommender.isShadowbanned,
            trustScore: this.recommender.trustScore,
          });
        }
      }
    }

    const collabBonusImpressions = input.collabPartner
      ? Math.round(
          Math.pow(Math.max(10, input.collabPartner.subscribers), 0.72) *
            clamp(input.collabPartner.synergy, 0.6, 1.6)
        )
      : 0;

    this.creatorSystem.applyVideoProductionEffort(Boolean(input.delegatedToTeam));

    const seq = this.videos.length + 1;
    const videoRecord: PublishedVideoRecord = {
      id: `vid-${seq}`,
      sequenceNumber: seq,
      title: input.packaging.variantA.title || input.title,
      niche: input.niche,
      publishedDay: this.clock.currentDay,
      publishedHour: this.clock.hourOfDay,
      isLiveSimulating: true,
      simulatedHours: 0,
      maxWaveReached: 0,
      totalImpressions: 0,
      totalViews: 0,
      avgCtr: 0,
      avgRetention: 0,
      likes: 0,
      dislikes: 0,
      commentsCount: 0,
      netSubscribers: 0,
      adSenseRevenueUsd: 0,
      sponsorRevenueUsd: sponsorPayout,
      productionCostUsd: budget,
      retentionCurve: editResult.retentionCurve.slice(),
      hourlyTimeline: [],
      topComments: [],
      postMortem: null,
    };

    this.videos.push(videoRecord);
    this.activeLiveContext = {
      video: videoRecord,
      input,
      editResult,
      evalA,
      evalB,
      intrinsicQuality,
      trendBoost,
      consistencyMultiplier,
      publishHourMultiplier,
      collabBonusImpressions,
      adAnnoyance,
      sketchySponsorLevel,
      currentViralScore: 0.46, // Стартовый кредит доверия на Wave 0
      variantAImpressions: 0,
      variantAViews: 0,
      variantBImpressions: 0,
      variantBViews: 0,
      lockedVariant: evalB ? null : 'A',
    };

    this.emit({ type: 'video:published', video: videoRecord });
    return videoRecord;
  }

  /**
   * Прогоняет ровно 1 игровой час активного 48-часового окна релиза.
   * Возвращает точку графика `VideoWavePoint` для живой отрисовки на 60 FPS!
   */
  stepLiveVideoHour(): VideoWavePoint | null {
    const ctx = this.activeLiveContext;
    if (!ctx) return null;

    const hourAfterPublish = ctx.video.simulatedHours + 1;

    // Выбор варианта обложки (в первые 4 часа идёт сплит A/B теста, с 5-го фиксируется лидер)
    let activeVariant: 'A' | 'B' = 'A';
    if (ctx.evalB) {
      if (hourAfterPublish <= 4) {
        activeVariant = hourAfterPublish % 2 === 1 ? 'A' : 'B';
      } else {
        if (!ctx.lockedVariant) {
          const ctrA =
            ctx.variantAImpressions > 0
              ? ctx.variantAViews / ctx.variantAImpressions
              : 0;
          const ctrB =
            ctx.variantBImpressions > 0
              ? ctx.variantBViews / ctx.variantBImpressions
              : 0;
          ctx.lockedVariant = ctrB > ctrA ? 'B' : 'A';
          if (ctx.lockedVariant === 'B' && ctx.input.packaging.variantB) {
            ctx.video.title = ctx.input.packaging.variantB.title;
          }
        }
        activeVariant = ctx.lockedVariant;
      }
    }

    const chosenPack =
      activeVariant === 'B' && ctx.evalB ? ctx.evalB : ctx.evalA;

    const { waveIndex, impressions } = this.recommender.planHourlyImpressions({
      hourAfterPublish,
      subscribers: this.audience.subscribers,
      currentViralScore: ctx.currentViralScore,
      publishHourMultiplier: ctx.publishHourMultiplier,
      worldPoolMultiplier: this.worldPoolMultiplierValue,
      collabBonusImpressions: ctx.collabBonusImpressions,
      prng: this.prng,
    });

    const waveEval = this.audience.evaluateWave(
      {
        waveIndex,
        impressions,
        videoNiche: ctx.video.niche,
        channelOriginNiche: this.creator.originNiche,
        currentDay: this.clock.currentDay,
        packagingAppeal: chosenPack.appealScore,
        clickbaitLevel: chosenPack.clickbaitRisk,
        intrinsicQuality: ctx.intrinsicQuality,
        retentionCurve: ctx.editResult.retentionCurve,
        trendCtrBoost: ctx.trendBoost,
        adAnnoyance: ctx.adAnnoyance,
      },
      this.prng
    );

    if (activeVariant === 'A') {
      ctx.variantAImpressions += waveEval.impressions;
      ctx.variantAViews += waveEval.views;
    } else {
      ctx.variantBImpressions += waveEval.impressions;
      ctx.variantBViews += waveEval.views;
    }

    // Обновляем накопительные метрики видео
    const prevViews = ctx.video.totalViews;
    ctx.video.simulatedHours = hourAfterPublish;
    if (waveIndex > ctx.video.maxWaveReached) {
      ctx.video.maxWaveReached = waveIndex;
    }
    ctx.video.totalImpressions += waveEval.impressions;
    ctx.video.totalViews += waveEval.views;
    ctx.video.avgCtr =
      ctx.video.totalImpressions > 0
        ? ctx.video.totalViews / ctx.video.totalImpressions
        : 0;
    ctx.video.avgRetention =
      ctx.video.totalViews > 0
        ? (ctx.video.avgRetention * prevViews +
            waveEval.avgWatchPct * waveEval.views) /
          ctx.video.totalViews
        : waveEval.avgWatchPct;

    ctx.video.likes += waveEval.likes;
    ctx.video.dislikes += waveEval.dislikes;
    ctx.video.commentsCount += waveEval.comments;
    ctx.video.netSubscribers += waveEval.subGains - waveEval.subLosses;
    ctx.video.retentionCurve = waveEval.observedRetentionCurve;

    // Партнёрская монетизация (если разблокирована на текущем этапе)
    if (this.stageDefinition.monetizationUnlocked && waveEval.views > 0) {
      const cpm = NICHE_SPECS[ctx.video.niche].baseCpmUsd;
      const retentionBonus = 0.65 + waveEval.avgWatchPct * 0.85;
      const hourlyRev = (waveEval.views / 1000) * cpm * retentionBonus;
      ctx.video.adSenseRevenueUsd = Number(
        (ctx.video.adSenseRevenueUsd + hourlyRev).toFixed(2)
      );
      this.creator.cashUsd = Number(
        (this.creator.cashUsd + hourlyRev).toFixed(2)
      );
    }

    // Пересчёт ViralScore по свежим накопленным сигналам
    ctx.currentViralScore = this.recommender.computeViralScore({
      niche: ctx.video.niche,
      waveIndex,
      observedCtr: ctx.video.avgCtr,
      observedRetention: ctx.video.avgRetention,
      views: ctx.video.totalViews,
      likes: ctx.video.likes,
      dislikes: ctx.video.dislikes,
      comments: ctx.video.commentsCount,
      subGains: Math.max(0, ctx.video.netSubscribers),
      clickbaitLevel: chosenPack.clickbaitRisk,
      intrinsicQuality: ctx.intrinsicQuality,
      sketchySponsorLevel: ctx.sketchySponsorLevel,
      trendBoost: ctx.trendBoost,
      consistencyMultiplier: ctx.consistencyMultiplier,
      worldPoolMultiplier: this.worldPoolMultiplierValue,
    });

    const point: VideoWavePoint = {
      hourAfterPublish,
      waveIndex,
      impressions: waveEval.impressions,
      views: waveEval.views,
      cumulativeViews: ctx.video.totalViews,
      ctr: Number(waveEval.ctr.toFixed(4)),
      avgWatchPct: Number(waveEval.avgWatchPct.toFixed(4)),
      likes: waveEval.likes,
      dislikes: waveEval.dislikes,
      comments: waveEval.comments,
      subGains: waveEval.subGains,
      subLosses: waveEval.subLosses,
      viralScore: Number(ctx.currentViralScore.toFixed(4)),
      activeVariant,
    };
    ctx.video.hourlyTimeline.push(point);

    // Продвигаем мировые часы на 1 час
    this.advanceHoursInternal(1);

    // Проверяем переход на новый этап канала («Новичок» -> «Растущий канал» -> ...)
    const stageSync = this.creatorSystem.syncStage(this.audience.subscribers);
    if (stageSync.upgraded) {
      this.emit({
        type: 'stage:upgraded',
        previous: stageSync.previous,
        current: stageSync.current,
      });
    }

    this.emit({
      type: 'video:wave_hour',
      videoId: ctx.video.id,
      point,
    });

    // На 48-м часе финализируем отчёт, генерируем живые комментарии и пост-мортем разбор
    if (hourAfterPublish >= 48) {
      this.finalizeLiveVideo(ctx);
    }

    return point;
  }

  /** Удобный метод полного цикла «публикация + все 48 часов реакции алгоритма» */
  publishAndSimulateFullVideo(input: VideoReleaseInput): PublishedVideoRecord {
    const record = this.beginVideoRelease(input);
    while (this.activeLiveContext !== null) {
      this.stepLiveVideoHour();
    }
    return record;
  }

  private finalizeLiveVideo(ctx: ActiveLiveSimulationContext): void {
    const winningVariant: 'A' | 'B' = ctx.lockedVariant ?? 'A';
    const chosenPack =
      winningVariant === 'B' && ctx.evalB ? ctx.evalB : ctx.evalA;

    // Если кликбейт оказался сильным обманом — алгоритм штрафует траст канала
    const clickbaitGap = chosenPack.clickbaitRisk - ctx.intrinsicQuality;
    if (clickbaitGap > 0.28) {
      const banChanged = this.recommender.adjustTrust(-clickbaitGap * 0.18);
      if (banChanged) {
        this.emit({
          type: 'shadowban:changed',
          active: this.recommender.isShadowbanned,
          trustScore: this.recommender.trustScore,
        });
      }
    }

    const ctrA =
      ctx.variantAImpressions > 0
        ? ctx.variantAViews / ctx.variantAImpressions
        : ctx.video.avgCtr;
    const ctrB =
      ctx.evalB && ctx.variantBImpressions > 0
        ? ctx.variantBViews / ctx.variantBImpressions
        : null;

    ctx.video.isLiveSimulating = false;
    ctx.video.topComments = this.comments.generateForVideo({
      video: ctx.video,
      isNicheShifted: ctx.video.niche !== this.creator.originNiche,
      hasSketchySponsor: ctx.sketchySponsorLevel > 0.4,
      clickbaitGap,
      weakestDecileIndex: ctx.editResult.weakestDecileIndex,
      prng: this.prng,
      count: 6,
    });

    ctx.video.postMortem = buildVideoPostMortem({
      video: ctx.video,
      regime: this.recommender.regime,
      isShadowbanned: this.recommender.isShadowbanned,
      clickbaitLevel: chosenPack.clickbaitRisk,
      intrinsicQuality: ctx.intrinsicQuality,
      weakestDecileIndex: ctx.editResult.weakestDecileIndex,
      winningVariant,
      variantACtr: ctrA,
      variantBCtr: ctrB,
    });

    this.activeLiveContext = null;
    this.emit({ type: 'video:completed', video: ctx.video });
  }

  // --- Межрелизная перемотка дней, стримы, команда, дилеммы ---

  advanceDays(days: number): ScheduledConsequence[] {
    if (days <= 0) return [];
    const prevDay = this.clock.currentDay;
    const triggered = this.advanceHoursInternal(days * 24);
    const nextDay = this.clock.currentDay;

    this.audience.advanceDays(days, nextDay);
    this.trends.advanceToDay(nextDay, this.prng);

    const quitters = this.creatorSystem.advanceDays(
      days,
      prevDay,
      nextDay,
      this.prng
    );
    for (const member of quitters) {
      this.emit({ type: 'team:member_quit', member });
    }

    // Периодическая смена эпохи алгоритма (в среднем раз в ~25 дней)
    if (this.prng.chance(Math.min(0.45, days * 0.035))) {
      this.triggerAlgorithmUpdate();
    }

    this.emit({
      type: 'day:advanced',
      day: this.clock.currentDay,
      hour: this.clock.hourOfDay,
    });

    return triggered;
  }

  triggerAlgorithmUpdate(): void {
    const prev = this.recommender.regimeId;
    const next = this.recommender.mutateRegime(this.prng);
    this.emit({ type: 'algorithm:mutated', previous: prev, current: next });
  }

  runLiveStream(input: StreamMiniGameInput): StreamMiniGameResult {
    const skills = this.creatorSystem.getEffectiveSkills();
    const res = solveStreamMiniGame(input, this.audience.subscribers, skills);

    this.creator.cashUsd = Number(
      (this.creator.cashUsd + res.donationsUsd).toFixed(2)
    );
    this.creator.energy = clamp(this.creator.energy - res.energySpent, 0, 100);
    this.creator.burnout = clamp(
      this.creator.burnout + res.burnoutAdded,
      0,
      100
    );
    this.audience.applyGlobalImpulse({
      flatSubscriberDelta: res.newSubscribers,
      loyaltyDelta: 0.03,
    });

    const stageSync = this.creatorSystem.syncStage(this.audience.subscribers);
    if (stageSync.upgraded) {
      this.emit({
        type: 'stage:upgraded',
        previous: stageSync.previous,
        current: stageSync.current,
      });
    }

    this.advanceHoursInternal(input.durationHours);
    return res;
  }

  chooseDilemmaOption(dilemmaId: string, choiceId: string): {
    scheduledConsequence: ScheduledConsequence | null;
  } {
    const { choice, scheduledConsequence } = this.dilemmas.resolveChoice({
      dilemmaId,
      choiceId,
      currentTotalHours: this.clock.totalHours,
      prng: this.prng,
    });

    this.creator.cashUsd = Math.max(0, this.creator.cashUsd + choice.immediateCashUsd);
    this.creator.energy = clamp(
      this.creator.energy + choice.immediateEnergyDelta,
      0,
      100
    );
    this.creator.reputation = clamp(
      this.creator.reputation + choice.immediateReputationDelta,
      0,
      100
    );

    if (choice.immediateTrustDelta !== 0) {
      const banChanged = this.recommender.adjustTrust(choice.immediateTrustDelta);
      if (banChanged) {
        this.emit({
          type: 'shadowban:changed',
          active: this.recommender.isShadowbanned,
          trustScore: this.recommender.trustScore,
        });
      }
    }

    if (scheduledConsequence) {
      this.clock.schedule(scheduledConsequence);
    }

    return { scheduledConsequence };
  }

  hireTeamMember(role: TeamRole, skill = 55): TeamMember {
    return this.creatorSystem.hireMember(role, skill, this.prng);
  }

  upgradeStudioEquipment(): { newTier: number; costUsd: number } {
    return this.creatorSystem.upgradeEquipment();
  }

  private advanceHoursInternal(hours: number): ScheduledConsequence[] {
    const due = this.clock.advanceHours(hours);
    for (const c of due) {
      this.applyConsequence(c);
    }
    return due;
  }

  private applyConsequence(c: ScheduledConsequence): void {
    this.creator.cashUsd = Math.max(0, this.creator.cashUsd + c.cashDeltaUsd);
    this.creator.reputation = clamp(
      this.creator.reputation + c.reputationDelta,
      0,
      100
    );
    this.creator.burnout = clamp(
      this.creator.burnout + c.burnoutDelta,
      0,
      100
    );

    const banChanged = this.recommender.adjustTrust(c.trustDelta);
    if (banChanged) {
      this.emit({
        type: 'shadowban:changed',
        active: this.recommender.isShadowbanned,
        trustScore: this.recommender.trustScore,
      });
    }

    this.audience.applyGlobalImpulse({
      loyaltyDelta: c.loyaltyDelta,
      nostalgiaDelta: c.nostalgiaDelta,
    });

    this.emit({ type: 'consequence:triggered', consequence: c });
  }

  // --- Снапшоты и восстановление состояния (сохранения + облачная синхронизация) ---

  createSnapshot(): SimulEngineSnapshot {
    if (this.activeLiveContext !== null) {
      throw new Error('Снапшот создаётся после завершения активного 48ч релиза');
    }
    return {
      version: SIMUL_SNAPSHOT_VERSION,
      initialSeed: this.initialSeed,
      prngState: this.prng.getState(),
      clock: this.clock.serialize(),
      algorithm: this.recommender.serialize(),
      creatorAndTeam: this.creatorSystem.serialize(),
      audience: this.audience.serialize(),
      trends: this.trends.serialize(),
      dilemmas: this.dilemmas.serialize(),
      videos: JSON.parse(JSON.stringify(this.videos)) as PublishedVideoRecord[],
      worldPoolMultiplier: this.worldPoolMultiplierValue,
    };
  }

  restoreSnapshot(snapshot: SimulEngineSnapshot): void {
    this.activeLiveContext = null;
    this.prng.setState(snapshot.prngState);
    this.clock.restore(snapshot.clock);
    this.recommender.restore(snapshot.algorithm);
    this.creatorSystem.restore(snapshot.creatorAndTeam);
    this.audience.restore(snapshot.audience);
    this.trends.restore(snapshot.trends);
    this.dilemmas.restore(snapshot.dilemmas);
    this.videos = JSON.parse(JSON.stringify(snapshot.videos)) as PublishedVideoRecord[];
    this.worldPoolMultiplierValue = snapshot.worldPoolMultiplier;
  }
}
