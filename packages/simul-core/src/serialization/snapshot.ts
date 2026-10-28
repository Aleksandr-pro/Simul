// ============================================================================
// @simul/core/serialization/snapshot — Полный детерминированный снапшот движка
// ============================================================================

import type { AudiencePoolSnapshot } from '../agents/audience-pool.js';
import type { AlgorithmSnapshot } from '../algorithm/recommender.js';
import type { ClockSnapshot } from '../clock/timeline.js';
import type { PrngStateTuple } from '../math/prng.js';
import type { CreatorAndTeamSystem } from '../team/creator-team.js';
import type { PublishedVideoRecord, TrendCard } from '../types.js';

export const SIMUL_SNAPSHOT_VERSION = '0.1.0' as const;

export interface SimulEngineSnapshot {
  version: typeof SIMUL_SNAPSHOT_VERSION;
  initialSeed: number;
  prngState: PrngStateTuple;
  clock: ClockSnapshot;
  algorithm: AlgorithmSnapshot;
  creatorAndTeam: ReturnType<CreatorAndTeamSystem['serialize']>;
  audience: AudiencePoolSnapshot;
  trends: {
    trends: TrendCard[];
    nextId: number;
  };
  dilemmas: {
    resolvedIds: string[];
    nextConsequenceSeq: number;
  };
  videos: PublishedVideoRecord[];
  worldPoolMultiplier: number;
}
