// ============================================================================
// @simul/core/agents/audience-pool — Data-Oriented SoA пул «живых» агентов-зрителей
// Хранит десятки тысяч зрителей в непрерывных TypedArray-буферах (Zero-GC в тике).
// При выходе канала за пределы capacity автоматически масштабирует вес когорты.
// ============================================================================

import { clamp } from '../math/curves.js';
import type { SeededPrng } from '../math/prng.js';
import {
  NICHE_ORDER,
  NICHE_SPECS,
  ViewerSegment,
  type AudienceSummary,
  type NicheId,
} from '../types.js';

export interface WaveEvaluationParams {
  waveIndex: 0 | 1 | 2 | 3 | 4;
  impressions: number;
  videoNiche: NicheId;
  channelOriginNiche: NicheId;
  currentDay: number;
  /** Привлекательность превью/заголовка (0..1) */
  packagingAppeal: number;
  /** Степень кликбейта (0..1) */
  clickbaitLevel: number;
  /** Истинное качество контента и сценария (0..1) */
  intrinsicQuality: number;
  /** 10 точек базового удержания монтажа (0..1) */
  retentionCurve: readonly number[];
  /** Множитель интереса от попадания в тренд */
  trendCtrBoost: number;
  /** Раздражающий фактор рекламы (0 = нет рекламы, до 1 = скам-интеграция) */
  adAnnoyance: number;
}

export interface WaveEvaluationReport {
  impressions: number;
  views: number;
  ctr: number;
  avgWatchPct: number;
  likes: number;
  dislikes: number;
  comments: number;
  subGains: number;
  subLosses: number;
  /** Распределение просмотров по сегментам для генерации живых комментариев */
  viewsBySegment: Record<ViewerSegment, number>;
  /** Выборочные точки отвала по децилям (0..9) */
  observedRetentionCurve: number[];
}

export interface AudiencePoolSnapshot {
  capacity: number;
  activeCount: number;
  macroSubscribers: number;
  active: number[];
  segment: number[];
  subscribed: number[];
  primaryNiche: number[];
  loyalty: number[];
  patience: number[];
  adTolerance: number[];
  nostalgia: number[];
  fatigue: number[];
  joinedDay: number[];
}

export class AudiencePool {
  public readonly capacity: number;
  private activeCountValue = 0;
  /** Реальное макро-число подписчиков (синхронизировано с микро-агентами и масштабируется до миллионов) */
  private macroSubscribersValue = 0;

  // --- SoA (Structure of Arrays) непрерывные буферы в памяти ---
  public readonly active: Uint8Array;
  public readonly segment: Uint8Array;
  public readonly subscribed: Uint8Array;
  public readonly primaryNiche: Uint8Array;
  public readonly loyalty: Float32Array;
  public readonly patience: Float32Array;
  public readonly adTolerance: Float32Array;
  public readonly nostalgia: Float32Array;
  public readonly fatigue: Float32Array;
  public readonly joinedDay: Uint16Array;

  // Временный буфер кривой удержания (без аллокаций в цикле)
  private readonly decileSurvivors = new Float64Array(10);

  constructor(capacity = 20_000) {
    if (capacity < 256) {
      throw new Error('AudiencePool capacity must be >= 256');
    }
    this.capacity = capacity;
    this.active = new Uint8Array(capacity);
    this.segment = new Uint8Array(capacity);
    this.subscribed = new Uint8Array(capacity);
    this.primaryNiche = new Uint8Array(capacity);
    this.loyalty = new Float32Array(capacity);
    this.patience = new Float32Array(capacity);
    this.adTolerance = new Float32Array(capacity);
    this.nostalgia = new Float32Array(capacity);
    this.fatigue = new Float32Array(capacity);
    this.joinedDay = new Uint16Array(capacity);
  }

  get activeCount(): number {
    return this.activeCountValue;
  }

  get subscribers(): number {
    return this.macroSubscribersValue;
  }

  /**
   * Заселяет стартовую экосистему зрителей платформы (при 0 подписчиков у игрока!).
   * 42% пула имеют интерес к стартовой нише игрока, остальные распределены по другим нишам.
   */
  seedPlatformPopulation(
    prng: SeededPrng,
    originNiche: NicheId,
    initialAgents = Math.min(this.capacity, 12_000)
  ): void {
    const count = Math.min(this.capacity, Math.max(256, initialAgents));
    const originIdx = NICHE_SPECS[originNiche].index;

    this.activeCountValue = count;
    this.macroSubscribersValue = 0;

    for (let i = 0; i < count; i++) {
      this.active[i] = 1;
      this.subscribed[i] = 0;

      // Распределение ролей в дикой природе платформы
      const roll = prng.nextFloat();
      if (roll < 0.84) {
        this.segment[i] = ViewerSegment.Casual;
      } else if (roll < 0.92) {
        this.segment[i] = ViewerSegment.Critic;
      } else if (roll < 0.975) {
        this.segment[i] = ViewerSegment.Hater;
      } else {
        this.segment[i] = ViewerSegment.BrandScout;
      }

      if (prng.chance(0.42)) {
        this.primaryNiche[i] = originIdx;
      } else {
        this.primaryNiche[i] = prng.intRange(0, NICHE_ORDER.length - 1);
      }

      this.loyalty[i] = prng.range(-0.08, 0.15);
      this.patience[i] = clamp(prng.gaussian(0.54, 0.18), 0.1, 0.98);
      this.adTolerance[i] = clamp(prng.gaussian(0.5, 0.2), 0.05, 0.95);
      this.nostalgia[i] = 0;
      this.fatigue[i] = prng.range(0, 0.08);
      this.joinedDay[i] = 0;
    }
  }

  /** Доращивает активный пул микро-агентов до полной ёмкости по мере роста канала */
  private ensureAgentCapacityForGrowth(prng: SeededPrng, targetNicheIdx: number): void {
    if (this.activeCountValue >= this.capacity) return;
    const growBy = Math.min(512, this.capacity - this.activeCountValue);
    const start = this.activeCountValue;
    for (let i = start; i < start + growBy; i++) {
      this.active[i] = 1;
      this.subscribed[i] = 0;
      this.segment[i] = prng.chance(0.88)
        ? ViewerSegment.Casual
        : ViewerSegment.Critic;
      this.primaryNiche[i] = prng.chance(0.55)
        ? targetNicheIdx
        : prng.intRange(0, NICHE_ORDER.length - 1);
      this.loyalty[i] = prng.range(-0.05, 0.12);
      this.patience[i] = clamp(prng.gaussian(0.55, 0.17), 0.12, 0.96);
      this.adTolerance[i] = clamp(prng.gaussian(0.5, 0.2), 0.05, 0.95);
      this.nostalgia[i] = 0;
      this.fatigue[i] = 0;
      this.joinedDay[i] = 0;
    }
    this.activeCountValue += growBy;
  }

  /**
   * Прогоняет волну показов через живых микро-агентов.
   * При огромных волнах (100k–2M показов) сэмплирует до 4 096 репрезентативных агентов
   * и точно масштабирует макро-результат без просадки FPS.
   */
  evaluateWave(params: WaveEvaluationParams, prng: SeededPrng): WaveEvaluationReport {
    const impressions = Math.max(0, Math.floor(params.impressions));
    const emptySegments: Record<ViewerSegment, number> = {
      [ViewerSegment.Casual]: 0,
      [ViewerSegment.Subscriber]: 0,
      [ViewerSegment.CoreFan]: 0,
      [ViewerSegment.Veteran]: 0,
      [ViewerSegment.Critic]: 0,
      [ViewerSegment.Hater]: 0,
      [ViewerSegment.BrandScout]: 0,
    };

    if (impressions === 0 || this.activeCountValue === 0) {
      return {
        impressions: 0,
        views: 0,
        ctr: 0,
        avgWatchPct: 0,
        likes: 0,
        dislikes: 0,
        comments: 0,
        subGains: 0,
        subLosses: 0,
        viewsBySegment: emptySegments,
        observedRetentionCurve: params.retentionCurve.slice(0, 10),
      };
    }

    const nicheSpec = NICHE_SPECS[params.videoNiche];
    const videoNicheIdx = nicheSpec.index;
    const isNicheShifted = params.videoNiche !== params.channelOriginNiche;

    if (impressions > 1_500) {
      this.ensureAgentCapacityForGrowth(prng, videoNicheIdx);
    }

    const sampleSize = Math.min(impressions, Math.min(this.activeCountValue, 3_200));
    const macroScale = impressions / sampleSize;

    // Доля показов, приходящаяся на подписчиков, падает с расширением волны
    const targetSubShareByWave = [0.68, 0.34, 0.16, 0.07, 0.03] as const;
    const desiredSubProb =
      this.macroSubscribersValue > 0
        ? targetSubShareByWave[params.waveIndex]
        : 0;

    this.decileSurvivors.fill(0);

    let sampleClicks = 0;
    let watchPctSum = 0;
    let sampleLikes = 0;
    let sampleDislikes = 0;
    let sampleComments = 0;
    let sampleSubGains = 0;
    let sampleSubLosses = 0;

    const expectationGap = params.intrinsicQuality - params.clickbaitLevel * 0.88;
    const baseNicheCtr = nicheSpec.baselineCtr;
    const activeTotal = this.activeCountValue;

    for (let s = 0; s < sampleSize; s++) {
      // Быстрый выбор подходящего агента под профиль волны (до 3 попыток фильтрации)
      let idx = prng.intRange(0, activeTotal - 1);
      const wantSub = prng.chance(desiredSubProb);
      for (let attempt = 0; attempt < 3; attempt++) {
        const candidate = prng.intRange(0, activeTotal - 1);
        const isSub = this.subscribed[candidate] === 1;
        const matchesNiche = this.primaryNiche[candidate] === videoNicheIdx;
        if (wantSub && isSub) {
          idx = candidate;
          break;
        }
        if (!wantSub && (params.waveIndex >= 3 || matchesNiche)) {
          idx = candidate;
          break;
        }
      }

      const seg = this.segment[idx] as ViewerSegment;
      const isSub = this.subscribed[idx] === 1;
      const sameNiche = this.primaryNiche[idx] === videoNicheIdx;
      const loy = this.loyalty[idx]!;
      const fat = this.fatigue[idx]!;
      const nost = this.nostalgia[idx]!;

      // 1. Расчёт индивидуальной вероятности клика (CTR)
      let clickProb =
        baseNicheCtr *
        (0.45 + 1.32 * params.packagingAppeal) *
        params.trendCtrBoost;

      if (sameNiche) {
        clickProb *= 1.24;
      } else {
        clickProb *= params.waveIndex >= 3 ? 0.82 : 0.56;
      }

      if (isSub) {
        clickProb *= 1.48 + Math.max(0, loy) * 0.55;
      }
      if (seg === ViewerSegment.CoreFan) clickProb *= 1.35;
      if (seg === ViewerSegment.Hater) clickProb *= 1.15; // Хейтеры охотно кликают, чтобы найти повод придраться
      if (seg === ViewerSegment.Veteran && isNicheShifted) {
        clickProb *= Math.max(0.45, 1.0 - nost * 0.45);
      }

      clickProb *= Math.max(0.35, 1.0 - fat * 0.55);
      clickProb = clamp(clickProb, 0.004, 0.62);

      if (!prng.chance(clickProb)) {
        continue;
      }

      // 2. Агент кликнул! Симулируем удержание по 10 децилям ролика
      sampleClicks += 1;
      emptySegments[seg] = (emptySegments[seg] ?? 0) + 1;

      const pat = this.patience[idx]!;
      const adTol = this.adTolerance[idx]!;
      let watchedDeciles = 0;

      for (let d = 0; d < 10; d++) {
        const baseStepRet = params.retentionCurve[d] ?? 0.5;
        let stepKeepProb = clamp(
          0.52 +
            0.46 * baseStepRet +
            (pat - 0.5) * 0.16 +
            loy * 0.07 -
            Math.max(0, params.adAnnoyance - adTol * 0.6) * (d === 3 || d === 4 ? 0.24 : 0.04),
          0.12,
          0.985
        );

        // На первых децилях сильный кликбейт при слабом контенте вызывает резкий отвал
        if (d <= 2 && expectationGap < -0.2) {
          stepKeepProb += expectationGap * 0.35;
        }

        if (prng.chance(stepKeepProb)) {
          watchedDeciles = d + 1;
          this.decileSurvivors[d] = (this.decileSurvivors[d] ?? 0) + 1;
        } else {
          break;
        }
      }

      const watchPct = clamp(
        (watchedDeciles + prng.nextFloat() * 0.75) / 10.5,
        0.03,
        1.0
      );
      watchPctSum += watchPct;

      // 3. Эмоциональная реакция и мутация состояния агента в буферах
      this.fatigue[idx] = clamp(fat + nicheSpec.fatigueRate * 0.65, 0, 1);

      if (isSub && isNicheShifted && (seg === ViewerSegment.Veteran || seg === ViewerSegment.CoreFan)) {
        this.nostalgia[idx] = clamp(nost + 0.14, 0, 1);
      } else if (!isNicheShifted && nost > 0) {
        this.nostalgia[idx] = Math.max(0, nost - 0.04);
      }

      const satisfaction =
        watchPct * 0.55 +
        params.intrinsicQuality * 0.45 +
        Math.min(0, expectationGap) * 0.65 -
        params.adAnnoyance * (1 - adTol) * 0.45;

      const nextLoyalty = clamp(
        loy + (satisfaction - 0.46) * 0.22,
        -1.0,
        1.0
      );
      this.loyalty[idx] = nextLoyalty;

      // Лайки / дизлайки / комментарии
      if (satisfaction >= 0.62 && seg !== ViewerSegment.Hater) {
        if (prng.chance(0.065 + (satisfaction - 0.6) * 0.18)) {
          sampleLikes += 1;
        }
      } else if (satisfaction < 0.34 || seg === ViewerSegment.Hater) {
        if (prng.chance(seg === ViewerSegment.Hater ? 0.22 : 0.075)) {
          sampleDislikes += 1;
        }
      }

      const commentImpulse =
        0.008 +
        Math.abs(satisfaction - 0.5) * 0.035 +
        (seg === ViewerSegment.Critic || seg === ViewerSegment.Veteran ? 0.018 : 0);
      if (prng.chance(commentImpulse)) {
        sampleComments += 1;
      }

      // Подписка / отписка и эволюция сегмента
      if (!isSub) {
        if (
          satisfaction >= 0.64 &&
          watchPct >= 0.55 &&
          seg !== ViewerSegment.Hater &&
          prng.chance(0.045 + (satisfaction - 0.6) * 0.24)
        ) {
          this.subscribed[idx] = 1;
          this.joinedDay[idx] = params.currentDay;
          if (seg === ViewerSegment.Casual) {
            this.segment[idx] = ViewerSegment.Subscriber;
          }
          sampleSubGains += 1;
        } else if (expectationGap < -0.35 && prng.chance(0.06)) {
          this.segment[idx] = ViewerSegment.Hater;
        }
      } else {
        // Уже подписанный агент может вырасти в CoreFan / Veteran или отписаться
        const tenure = Math.max(0, params.currentDay - (this.joinedDay[idx] ?? 0));
        if (nextLoyalty > 0.65 && seg === ViewerSegment.Subscriber) {
          this.segment[idx] = ViewerSegment.CoreFan;
        } else if (tenure >= 45 && seg === ViewerSegment.CoreFan && nextLoyalty > 0.4) {
          this.segment[idx] = ViewerSegment.Veteran;
        }

        const unsubProb =
          (nextLoyalty < -0.25 ? 0.14 : 0) +
          (expectationGap < -0.32 ? 0.05 : 0) +
          (this.nostalgia[idx]! > 0.72 ? 0.06 : 0);
        if (unsubProb > 0 && prng.chance(unsubProb)) {
          this.subscribed[idx] = 0;
          this.segment[idx] =
            nextLoyalty < -0.45 ? ViewerSegment.Hater : ViewerSegment.Casual;
          sampleSubLosses += 1;
        }
      }
    }

    const views = Math.min(impressions, Math.round(sampleClicks * macroScale));
    const likes = Math.round(sampleLikes * macroScale);
    const dislikes = Math.round(sampleDislikes * macroScale);
    const comments = Math.round(sampleComments * macroScale);
    const subGains = Math.round(sampleSubGains * macroScale);
    const subLosses = Math.min(
      this.macroSubscribersValue + subGains,
      Math.round(sampleSubLosses * macroScale)
    );

    this.macroSubscribersValue = Math.max(
      0,
      this.macroSubscribersValue + subGains - subLosses
    );

    const scaledSegments: Record<ViewerSegment, number> = {
      [ViewerSegment.Casual]: Math.round(emptySegments[ViewerSegment.Casual] * macroScale),
      [ViewerSegment.Subscriber]: Math.round(emptySegments[ViewerSegment.Subscriber] * macroScale),
      [ViewerSegment.CoreFan]: Math.round(emptySegments[ViewerSegment.CoreFan] * macroScale),
      [ViewerSegment.Veteran]: Math.round(emptySegments[ViewerSegment.Veteran] * macroScale),
      [ViewerSegment.Critic]: Math.round(emptySegments[ViewerSegment.Critic] * macroScale),
      [ViewerSegment.Hater]: Math.round(emptySegments[ViewerSegment.Hater] * macroScale),
      [ViewerSegment.BrandScout]: Math.round(emptySegments[ViewerSegment.BrandScout] * macroScale),
    };

    const observedRetentionCurve = new Array<number>(10);
    for (let d = 0; d < 10; d++) {
      observedRetentionCurve[d] =
        sampleClicks > 0
          ? clamp((this.decileSurvivors[d] ?? 0) / sampleClicks, 0, 1)
          : (params.retentionCurve[d] ?? 0);
    }

    return {
      impressions,
      views,
      ctr: impressions > 0 ? views / impressions : 0,
      avgWatchPct: sampleClicks > 0 ? watchPctSum / sampleClicks : 0,
      likes,
      dislikes,
      comments,
      subGains,
      subLosses,
      viewsBySegment: scaledSegments,
      observedRetentionCurve,
    };
  }

  /** Межрелизное остывание усталости и взросление аудитории при перемотке дней */
  advanceDays(days: number, currentDay: number): void {
    if (days <= 0) return;
    const fatigueRecovery = Math.min(0.85, days * 0.045);
    const total = this.activeCountValue;

    for (let i = 0; i < total; i++) {
      this.fatigue[i] = Math.max(0, this.fatigue[i]! - fatigueRecovery);
      if (this.subscribed[i] === 1 && this.segment[i] === ViewerSegment.CoreFan) {
        const tenure = currentDay - (this.joinedDay[i] ?? 0);
        if (tenure >= 45) {
          this.segment[i] = ViewerSegment.Veteran;
        }
      }
    }
  }

  /** Глобальный импульс от последствий дилемм / скандалов / коллабораций */
  applyGlobalImpulse(opts: {
    loyaltyDelta?: number;
    nostalgiaDelta?: number;
    subscriberMultiplier?: number;
    flatSubscriberDelta?: number;
  }): void {
    const loyD = opts.loyaltyDelta ?? 0;
    const nostD = opts.nostalgiaDelta ?? 0;
    const total = this.activeCountValue;

    if (loyD !== 0 || nostD !== 0) {
      for (let i = 0; i < total; i++) {
        if (loyD !== 0) {
          this.loyalty[i] = clamp(this.loyalty[i]! + loyD, -1, 1);
        }
        if (nostD !== 0 && this.subscribed[i] === 1) {
          this.nostalgia[i] = clamp(this.nostalgia[i]! + nostD, 0, 1);
        }
      }
    }

    if (opts.subscriberMultiplier !== undefined) {
      this.macroSubscribersValue = Math.max(
        0,
        Math.round(this.macroSubscribersValue * opts.subscriberMultiplier)
      );
    }
    if (opts.flatSubscriberDelta !== undefined) {
      this.macroSubscribersValue = Math.max(
        0,
        this.macroSubscribersValue + Math.round(opts.flatSubscriberDelta)
      );
    }
  }

  /** Сводная статистика живого пула зрителей для UI и аналитики */
  summarize(): AudienceSummary {
    const counts: Record<ViewerSegment, number> = {
      [ViewerSegment.Casual]: 0,
      [ViewerSegment.Subscriber]: 0,
      [ViewerSegment.CoreFan]: 0,
      [ViewerSegment.Veteran]: 0,
      [ViewerSegment.Critic]: 0,
      [ViewerSegment.Hater]: 0,
      [ViewerSegment.BrandScout]: 0,
    };

    const total = this.activeCountValue;
    if (total === 0) {
      return {
        activeAgents: 0,
        capacity: this.capacity,
        subscribers: this.macroSubscribersValue,
        avgLoyalty: 0,
        avgFatigue: 0,
        avgNostalgia: 0,
        segmentCounts: counts,
      };
    }

    let loySum = 0;
    let fatSum = 0;
    let nostSum = 0;

    for (let i = 0; i < total; i++) {
      const seg = this.segment[i] as ViewerSegment;
      counts[seg] = (counts[seg] ?? 0) + 1;
      loySum += this.loyalty[i]!;
      fatSum += this.fatigue[i]!;
      nostSum += this.nostalgia[i]!;
    }

    return {
      activeAgents: total,
      capacity: this.capacity,
      subscribers: this.macroSubscribersValue,
      avgLoyalty: loySum / total,
      avgFatigue: fatSum / total,
      avgNostalgia: nostSum / total,
      segmentCounts: counts,
    };
  }

  serialize(): AudiencePoolSnapshot {
    const n = this.activeCountValue;
    return {
      capacity: this.capacity,
      activeCount: n,
      macroSubscribers: this.macroSubscribersValue,
      active: Array.from(this.active.subarray(0, n)),
      segment: Array.from(this.segment.subarray(0, n)),
      subscribed: Array.from(this.subscribed.subarray(0, n)),
      primaryNiche: Array.from(this.primaryNiche.subarray(0, n)),
      loyalty: Array.from(this.loyalty.subarray(0, n)),
      patience: Array.from(this.patience.subarray(0, n)),
      adTolerance: Array.from(this.adTolerance.subarray(0, n)),
      nostalgia: Array.from(this.nostalgia.subarray(0, n)),
      fatigue: Array.from(this.fatigue.subarray(0, n)),
      joinedDay: Array.from(this.joinedDay.subarray(0, n)),
    };
  }

  restore(snap: AudiencePoolSnapshot): void {
    const n = Math.min(this.capacity, snap.activeCount);
    this.activeCountValue = n;
    this.macroSubscribersValue = snap.macroSubscribers;
    this.active.fill(0);
    this.active.set(snap.active.slice(0, n));
    this.segment.set(snap.segment.slice(0, n));
    this.subscribed.set(snap.subscribed.slice(0, n));
    this.primaryNiche.set(snap.primaryNiche.slice(0, n));
    this.loyalty.set(snap.loyalty.slice(0, n));
    this.patience.set(snap.patience.slice(0, n));
    this.adTolerance.set(snap.adTolerance.slice(0, n));
    this.nostalgia.set(snap.nostalgia.slice(0, n));
    this.fatigue.set(snap.fatigue.slice(0, n));
    this.joinedDay.set(snap.joinedDay.slice(0, n));
  }
}
