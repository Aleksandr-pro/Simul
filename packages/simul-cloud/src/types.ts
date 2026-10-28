// ============================================================================
// @simul/cloud/types — Контракт синхронизации асинхронного общего мира
// ============================================================================

import type {
  ChannelStage,
  NicheId,
  PublishedVideoRecord,
  SimulEngineSnapshot,
} from '@simul/core';

export interface LeaderboardEntry {
  rank: number;
  handle: string;
  displayName: string;
  niche: NicheId;
  stage: ChannelStage;
  subscribers: number;
  totalViews: number;
  isPlayer: boolean;
}

export interface WorldNichePoolState {
  seasonId: string;
  niche: NicheId;
  competitionHeat: number;
  poolMultiplier: number;
  viralVideosLast24h: number;
  recentViralHeadlineRu: string | null;
}

export interface WorldSyncAdapter {
  readonly mode: 'local' | 'supabase';
  readonly seasonId: string;

  /** Получает состояние общего пула показов ниши (с учётом вирусных видео соперников) */
  fetchNichePoolState(niche: NicheId): Promise<WorldNichePoolState>;

  /** Отправляет результат релиза в общий мир и возвращает обновлённое давление пула */
  submitVideoRelease(opts: {
    handle: string;
    displayName: string;
    stage: ChannelStage;
    subscribers: number;
    totalViews: number;
    inGameDay: number;
    video: PublishedVideoRecord;
    runSeed: number;
  }): Promise<WorldNichePoolState>;

  /** Загружает таблицу лидеров ниши */
  fetchLeaderboard(
    niche: NicheId,
    playerState?: {
      handle: string;
      displayName: string;
      stage: ChannelStage;
      subscribers: number;
      totalViews: number;
    }
  ): Promise<LeaderboardEntry[]>;

  /** Сохраняет снапшот движка */
  saveSnapshot(slotKey: string, snapshot: SimulEngineSnapshot): Promise<void>;

  /** Загружает снапшот движка */
  loadSnapshot(slotKey: string): Promise<SimulEngineSnapshot | null>;
}
