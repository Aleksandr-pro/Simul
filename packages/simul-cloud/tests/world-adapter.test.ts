import { describe, expect, it } from 'vitest';
import { SimulEngine } from '@simul/core';
import { LocalWorldAdapter, createWorldSyncAdapter } from '@simul/cloud';

describe('@simul/cloud — Общий пул показов и адаптеры синхронизации', () => {
  it('переключается на LocalWorldAdapter без ключей и обновляет пул ниши', async () => {
    const adapter = createWorldSyncAdapter({});
    expect(adapter.mode).toBe('local');

    const engine = new SimulEngine({ seed: 1234, originNiche: 'tech' });
    const initialPool = await adapter.fetchNichePoolState('tech');
    expect(initialPool.poolMultiplier).toBeGreaterThan(0.5);

    const vid = engine.publishAndSimulateFullVideo({
      title: 'Вирусный техно-разбор',
      niche: 'tech',
      productionBudgetUsd: 150,
      publishHourOfDay: 19,
      editing: {
        hookStrength: 0.9,
        pacingTightness: 0.88,
        payoffStrength: 0.85,
        adPlacementRatio: null,
      },
      packaging: {
        variantA: {
          id: 'A',
          title: 'Вирусный техно-разбор',
          visualContrast: 0.9,
          curiosityGap: 0.85,
          clickbaitLevel: 0.1,
        },
      },
    });

    const updatedPool = await adapter.submitVideoRelease({
      handle: engine.creator.handle,
      displayName: engine.creator.displayName,
      stage: engine.stage,
      subscribers: engine.subscribers,
      totalViews: engine.totalViews,
      inGameDay: engine.clock.currentDay,
      video: vid,
      runSeed: engine.initialSeed,
    });

    expect(updatedPool.niche).toBe('tech');

    const board = await adapter.fetchLeaderboard('tech', {
      handle: engine.creator.handle,
      displayName: engine.creator.displayName,
      stage: engine.stage,
      subscribers: engine.subscribers,
      totalViews: engine.totalViews,
    });
    expect(board.some((e) => e.isPlayer)).toBe(true);

    const local = new LocalWorldAdapter('season-test');
    const snap = engine.createSnapshot();
    await local.saveSnapshot('career-1', snap);
    const loaded = await local.loadSnapshot('career-1');
    expect(loaded?.initialSeed).toBe(1234);
  });
});
