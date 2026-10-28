// ============================================================================
// @simul/cloud/supabase-adapter — Боевой адаптер асинхронного мира на Supabase
// Работает поверх таблиц и PL/pgSQL RPC из supabase/migrations/0001_simul_async_world.sql
// ============================================================================

import { createClient, type SupabaseClient } from '@supabase/supabase-js';
import {
  clamp,
  type ChannelStage,
  type NicheId,
  type PublishedVideoRecord,
  type SimulEngineSnapshot,
} from '@simul/core';
import { LocalWorldAdapter } from './local-adapter.js';
import type {
  LeaderboardEntry,
  WorldNichePoolState,
  WorldSyncAdapter,
} from './types.js';

export interface SupabaseWorldConfig {
  supabaseUrl: string;
  supabasePublishableKey: string;
  seasonId?: string;
  channelId?: string;
}

export class SupabaseWorldAdapter implements WorldSyncAdapter {
  public readonly mode = 'supabase' as const;
  public readonly seasonId: string;
  public readonly client: SupabaseClient;
  private channelId: string | null;

  constructor(config: SupabaseWorldConfig) {
    this.seasonId = config.seasonId ?? 'season-1';
    this.channelId = config.channelId ?? null;
    this.client = createClient(config.supabaseUrl, config.supabasePublishableKey, {
      auth: {
        persistSession: true,
        autoRefreshToken: true,
      },
    });
  }

  setChannelId(channelId: string): void {
    this.channelId = channelId;
  }

  async fetchNichePoolState(niche: NicheId): Promise<WorldNichePoolState> {
    const [{ data: poolRow }, { data: rpcMultiplier }] = await Promise.all([
      this.client
        .from('simul_niche_pools')
        .select('competition_heat, viral_videos_24h')
        .eq('season_id', this.seasonId)
        .eq('niche', niche)
        .maybeSingle(),
      this.client.rpc('claim_niche_pool_multiplier', {
        p_season_id: this.seasonId,
        p_niche: niche,
      }),
    ]);

    const heat = Number(poolRow?.competition_heat ?? 1.0);
    const mult =
      typeof rpcMultiplier === 'number' || typeof rpcMultiplier === 'string'
        ? Number(rpcMultiplier)
        : clamp(1 / Math.max(0.75, heat), 0.58, 1.22);

    return {
      seasonId: this.seasonId,
      niche,
      competitionHeat: Number(heat.toFixed(4)),
      poolMultiplier: Number(mult.toFixed(4)),
      viralVideosLast24h: Number(poolRow?.viral_videos_24h ?? 0),
      recentViralHeadlineRu:
        heat > 1.18
          ? 'В общем мире Supabase зафиксирован всплеск вирусных видео в вашей нише!'
          : null,
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
    if (this.channelId) {
      await this.client.rpc('record_world_video_release', {
        p_channel_id: this.channelId,
        p_title: opts.video.title,
        p_viral_score: Number(
          (
            opts.video.hourlyTimeline[opts.video.hourlyTimeline.length - 1]
              ?.viralScore ?? 0.5
          ).toFixed(4)
        ),
        p_max_wave: opts.video.maxWaveReached,
        p_views: opts.video.totalViews,
        p_ctr: Number(opts.video.avgCtr.toFixed(4)),
        p_retention: Number(opts.video.avgRetention.toFixed(4)),
        p_subscribers: opts.subscribers,
        p_total_views: opts.totalViews,
        p_in_game_day: opts.inGameDay,
        p_run_seed: opts.runSeed,
      });
    }
    return this.fetchNichePoolState(opts.video.niche);
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
    const { data, error } = await this.client
      .from('simul_channels')
      .select('handle, display_name, niche, stage, subscribers, total_views')
      .eq('season_id', this.seasonId)
      .eq('niche', niche)
      .order('subscribers', { ascending: false })
      .limit(20);

    if (error || !data) {
      return [];
    }

    return data.map((row, idx) => ({
      rank: idx + 1,
      handle: String(row.handle),
      displayName: String(row.display_name),
      niche: row.niche as NicheId,
      stage: row.stage as ChannelStage,
      subscribers: Number(row.subscribers),
      totalViews: Number(row.total_views),
      isPlayer: playerState ? row.handle === playerState.handle : false,
    }));
  }

  async saveSnapshot(_slotKey: string, snapshot: SimulEngineSnapshot): Promise<void> {
    if (!this.channelId) return;
    const { data: userRes } = await this.client.auth.getUser();
    const userId = userRes.user?.id;
    if (!userId) return;

    await this.client.from('simul_channel_snapshots').upsert({
      channel_id: this.channelId,
      owner_id: userId,
      in_game_day: Math.floor(snapshot.clock.totalHours / 24) + 1,
      in_game_hour: snapshot.clock.totalHours % 24,
      engine_version: snapshot.version,
      snapshot_payload: snapshot,
      updated_at: new Date().toISOString(),
    });
  }

  async loadSnapshot(_slotKey: string): Promise<SimulEngineSnapshot | null> {
    if (!this.channelId) return null;
    const { data } = await this.client
      .from('simul_channel_snapshots')
      .select('snapshot_payload')
      .eq('channel_id', this.channelId)
      .maybeSingle();

    if (!data?.snapshot_payload) return null;
    return data.snapshot_payload as SimulEngineSnapshot;
  }
}

/**
 * Автоматически создаёт SupabaseWorldAdapter при наличии ключей окружения,
 * либо бесшовно переключается на LocalWorldAdapter с ИИ-конкурентами.
 */
export function createWorldSyncAdapter(opts?: {
  supabaseUrl?: string;
  supabasePublishableKey?: string;
  seasonId?: string;
}): WorldSyncAdapter {
  const url = opts?.supabaseUrl?.trim();
  const key = opts?.supabasePublishableKey?.trim();
  if (
    url &&
    key &&
    url.startsWith('https://') &&
    !url.includes('your-project-ref')
  ) {
    return new SupabaseWorldAdapter({
      supabaseUrl: url,
      supabasePublishableKey: key,
      seasonId: opts?.seasonId,
    });
  }
  return new LocalWorldAdapter(opts?.seasonId ?? 'season-1');
}
