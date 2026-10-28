// ============================================================================
// @simul/cloud/local-adapter — Автономный адаптер общего мира с ИИ-конкурентами
// Симулирует живую конкуренцию за показы ниши и сохраняет снапшоты локально.
// ============================================================================

import {
  NICHE_ORDER,
  SeededPrng,
  clamp,
  resolveStageForSubscribers,
  type NicheId,
  type PublishedVideoRecord,
  type SimulEngineSnapshot,
  type ChannelStage,
} from '@simul/core';
import type {
  LeaderboardEntry,
  WorldNichePoolState,
  WorldSyncAdapter,
} from './types.js';

interface SimulatedRival {
  handle: string;
  displayName: string;
  niche: NicheId;
  subscribers: number;
  totalViews: number;
}

const INITIAL_RIVALS: readonly SimulatedRival[] = [
  { handle: '@cyber_max', displayName: 'КиберМакс Обзоры', niche: 'tech', subscribers: 185_000, totalViews: 14_200_000 },
  { handle: '@nano_lab', displayName: 'НаноЛаб', niche: 'tech', subscribers: 42_000, totalViews: 2_900_000 },
  { handle: '@volt_bench', displayName: 'Вольт Тест', niche: 'tech', subscribers: 9_400, totalViews: 610_000 },
  { handle: '@frag_queen', displayName: 'FragQueen Stream', niche: 'gaming', subscribers: 310_000, totalViews: 28_500_000 },
  { handle: '@indie_pixel', displayName: 'Пиксельный Угол', niche: 'gaming', subscribers: 24_500, totalViews: 1_750_000 },
  { handle: '@cozy_mira', displayName: 'Мира Влог', niche: 'lifestyle', subscribers: 120_000, totalViews: 8_400_000 },
  { handle: '@brain_spark', displayName: 'Искра Науки', niche: 'edutainment', subscribers: 260_000, totalViews: 19_100_000 },
  { handle: '@mega_challenge', displayName: 'Шоу Без Границ', niche: 'entertainment', subscribers: 890_000, totalViews: 94_000_000 },
];

export class LocalWorldAdapter implements WorldSyncAdapter {
  public readonly mode = 'local' as const;
  public readonly seasonId: string;

  private readonly prng = new SeededPrng(991042);
  private readonly rivals: SimulatedRival[];
  private readonly heatByNiche: Record<NicheId, number> = {
    gaming: 1.04,
    tech: 0.98,
    lifestyle: 1.0,
    edutainment: 0.94,
    entertainment: 1.1,
  };
  private readonly viralCountByNiche: Record<NicheId, number> = {
    gaming: 2,
    tech: 1,
    lifestyle: 1,
    edutainment: 1,
    entertainment: 3,
  };
  private readonly headlineByNiche: Record<NicheId, string | null> = {
    gaming: null,
    tech: null,
    lifestyle: null,
    edutainment: null,
    entertainment: null,
  };
  private readonly memoryStore = new Map<string, string>();

  constructor(seasonId = 'season-1') {
    this.seasonId = seasonId;
    this.rivals = INITIAL_RIVALS.map((r) => ({ ...r }));
  }

  async fetchNichePoolState(niche: NicheId): Promise<WorldNichePoolState> {
    const heat = this.heatByNiche[niche];
    const poolMultiplier = Number(
      clamp(1 / Math.max(0.75, heat), 0.58, 1.22).toFixed(4)
    );
    return {
      seasonId: this.seasonId,
      niche,
      competitionHeat: Number(heat.toFixed(3)),
      poolMultiplier,
      viralVideosLast24h: this.viralCountByNiche[niche],
      recentViralHeadlineRu: this.headlineByNiche[niche],
    };
  }

  async submitVideoRelease(opts: {
    handle: string;
    displayName: string;
    stage: ChannelStage;
    subscribers: number;
    totalViews: number;
    inGameDay: number;
    video: PublishedVideoRecord;
    runSeed: number;
  }): Promise<WorldNichePoolState> {
    // 1. Живая эволюция ИИ-конкурентов мира
    for (const r of this.rivals) {
      const growth = this.prng.range(0.995, 1.032);
      r.subscribers = Math.round(r.subscribers * growth);
      r.totalViews += Math.round(r.subscribers * this.prng.range(0.15, 0.85));
    }

    // 2. Естественное остывание накала во всех нишах + шанс вирусного хита у конкурента
    for (const n of NICHE_ORDER) {
      let nextHeat = 1.0 + (this.heatByNiche[n] - 1.0) * 0.85;
      if (this.prng.chance(0.18)) {
        const rival = this.rivals.find((r) => r.niche === n);
        nextHeat += 0.14;
        this.viralCountByNiche[n] += 1;
        this.headlineByNiche[n] = rival
          ? `У конкурента ${rival.displayName} выстрелил ролик и оттянул часть показов ниши!`
          : 'В нише вспыхнул вирусный тренд — борьба за ленту обострилась!';
      } else {
        this.viralCountByNiche[n] = Math.max(0, this.viralCountByNiche[n] - 1);
      }
      this.heatByNiche[n] = clamp(nextHeat, 0.78, 1.85);
    }

    // 3. Собственный прорыв игрока на Wave 3..4 тоже разогревает пул ниши
    const videoNiche = opts.video.niche;
    if (opts.video.maxWaveReached >= 3) {
      this.heatByNiche[videoNiche] = clamp(
        this.heatByNiche[videoNiche] + (opts.video.maxWaveReached === 4 ? 0.15 : 0.07),
        0.78,
        1.95
      );
      this.viralCountByNiche[videoNiche] += 1;
      this.headlineByNiche[videoNiche] =
        `Ваш ролик «${opts.video.title}» ворвался в тренды и доминирует в выдаче!`;
    }

    return this.fetchNichePoolState(videoNiche);
  }

  async fetchLeaderboard(
    niche: NicheId,
    playerState?: {
      handle: string;
      displayName: string;
      stage: ChannelStage;
      subscribers: number;
      totalViews: number;
    }
  ): Promise<LeaderboardEntry[]> {
    const entries: Omit<LeaderboardEntry, 'rank'>[] = this.rivals
      .filter((r) => r.niche === niche)
      .map((r) => ({
        handle: r.handle,
        displayName: r.displayName,
        niche: r.niche,
        stage: resolveStageForSubscribers(r.subscribers).id,
        subscribers: r.subscribers,
        totalViews: r.totalViews,
        isPlayer: false,
      }));

    if (playerState) {
      entries.push({
        handle: playerState.handle,
        displayName: playerState.displayName,
        niche,
        stage: playerState.stage,
        subscribers: playerState.subscribers,
        totalViews: playerState.totalViews,
        isPlayer: true,
      });
    }

    entries.sort((a, b) => b.subscribers - a.subscribers || b.totalViews - a.totalViews);
    return entries.map((item, idx) => ({
      rank: idx + 1,
      ...item,
    }));
  }

  async saveSnapshot(slotKey: string, snapshot: SimulEngineSnapshot): Promise<void> {
    const serialized = JSON.stringify(snapshot);
    const fullKey = `simul:save:${this.seasonId}:${slotKey}`;
    if (typeof localStorage !== 'undefined') {
      localStorage.setItem(fullKey, serialized);
    } else {
      this.memoryStore.set(fullKey, serialized);
    }
  }

  async loadSnapshot(slotKey: string): Promise<SimulEngineSnapshot | null> {
    const fullKey = `simul:save:${this.seasonId}:${slotKey}`;
    const raw =
      typeof localStorage !== 'undefined'
        ? localStorage.getItem(fullKey)
        : (this.memoryStore.get(fullKey) ?? null);
    if (!raw) return null;
    return JSON.parse(raw) as SimulEngineSnapshot;
  }
}
