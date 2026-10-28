import { describe, expect, it } from 'vitest';
import {
  SimulEngine,
  ViewerSegment,
  type ScheduledConsequence,
  type VideoReleaseInput,
} from '@simul/core';

function makeStrongVideo(title = 'Сильный выпуск'): VideoReleaseInput {
  return {
    title,
    niche: 'tech',
    productionBudgetUsd: 180,
    publishHourOfDay: 19,
    editing: {
      hookStrength: 0.92,
      pacingTightness: 0.88,
      payoffStrength: 0.90,
      adPlacementRatio: null,
    },
    packaging: {
      variantA: {
        id: 'A',
        title,
        visualContrast: 0.88,
        curiosityGap: 0.86,
        clickbaitLevel: 0.14,
      },
    },
  };
}

function makeWeakClickbaitVideo(title = 'ШОК! СЕНСАЦИЯ?!'): VideoReleaseInput {
  return {
    title,
    niche: 'tech',
    productionBudgetUsd: 0,
    publishHourOfDay: 4,
    sponsorDeal: {
      brandName: 'ScamToken',
      payoutUsd: 500,
      sketchiness: 0.95,
    },
    editing: {
      hookStrength: 0.18,
      pacingTightness: 0.20,
      payoffStrength: 0.15,
      adPlacementRatio: 0.05,
    },
    packaging: {
      variantA: {
        id: 'A',
        title,
        visualContrast: 0.45,
        curiosityGap: 0.40,
        clickbaitLevel: 0.92,
      },
    },
  };
}

describe('SimulEngine — Детерминизм, алгоритм, живые агенты и снапшоты', () => {
  it('гарантирует бит-в-бит идентичную симуляцию при одинаковом seed', () => {
    const engineA = new SimulEngine({ seed: 424242, originNiche: 'tech' });
    const engineB = new SimulEngine({ seed: 424242, originNiche: 'tech' });

    const vidA = engineA.publishAndSimulateFullVideo(makeStrongVideo());
    const vidB = engineB.publishAndSimulateFullVideo(makeStrongVideo());

    expect(vidA.totalViews).toBe(vidB.totalViews);
    expect(vidA.totalImpressions).toBe(vidB.totalImpressions);
    expect(vidA.avgCtr).toBe(vidB.avgCtr);
    expect(vidA.avgRetention).toBe(vidB.avgRetention);
    expect(engineA.subscribers).toBe(engineB.subscribers);
  });

  it('продвигает сильное видео на высокие волны и тормозит слабый кликбейт', () => {
    const goodEngine = new SimulEngine({ seed: 1001, originNiche: 'tech' });
    const badEngine = new SimulEngine({ seed: 1001, originNiche: 'tech' });

    const goodVid = goodEngine.publishAndSimulateFullVideo(makeStrongVideo());
    const badVid = badEngine.publishAndSimulateFullVideo(makeWeakClickbaitVideo());

    expect(goodVid.maxWaveReached).toBeGreaterThan(badVid.maxWaveReached);
    expect(goodVid.totalViews).toBeGreaterThan(badVid.totalViews * 3);
    expect(goodVid.postMortem).not.toBeNull();
  });

  it('корректно выбирает победителя в A/B-тесте обложек на 5-м часе', () => {
    const engine = new SimulEngine({ seed: 2026, originNiche: 'gaming' });
    const vid = engine.publishAndSimulateFullVideo({
      title: 'Слабый заголовок',
      niche: 'gaming',
      productionBudgetUsd: 100,
      publishHourOfDay: 19,
      editing: {
        hookStrength: 0.8,
        pacingTightness: 0.8,
        payoffStrength: 0.8,
        adPlacementRatio: null,
      },
      packaging: {
        variantA: {
          id: 'A',
          title: 'Коротко',
          visualContrast: 0.2,
          curiosityGap: 0.2,
          clickbaitLevel: 0.05,
        },
        variantB: {
          id: 'B',
          title: 'Я прошёл невозможный рейд в одиночку без брони',
          visualContrast: 0.92,
          curiosityGap: 0.90,
          clickbaitLevel: 0.15,
        },
      },
    });

    expect(vid.postMortem?.winningVariant).toBe('B');
    expect(vid.hourlyTimeline[10]?.activeVariant).toBe('B');
  });

  it('включает шадоубан при систематическом обмане и скам-интеграциях', () => {
    const engine = new SimulEngine({ seed: 909, originNiche: 'tech' });
    for (let i = 0; i < 4; i++) {
      engine.publishAndSimulateFullVideo(makeWeakClickbaitVideo(`Скам #${i}`));
      engine.advanceDays(3);
    }

    expect(engine.recommender.trustScore).toBeLessThan(0.35);
    expect(engine.recommender.isShadowbanned).toBe(true);

    // Даже отличный ролик под шадоубаном не пускают дальше Wave 0
    const blockedGood = engine.publishAndSimulateFullVideo(makeStrongVideo());
    expect(blockedGood.maxWaveReached).toBe(0);
  });

  it('отрабатывает дилеммы и срабатывание отложенных последствий спустя недели', () => {
    const engine = new SimulEngine({ seed: 77, originNiche: 'tech' });
    // Поднимаем подписчиков до этапа «Растущий канал» (1 000+)
    engine.audience.applyGlobalImpulse({ flatSubscriberDelta: 2_500 });
    engine.creatorSystem.syncStage(engine.subscribers);

    const dilemmas = engine.getAvailableDilemmas();
    expect(dilemmas.length).toBeGreaterThan(0);

    const triggeredList: ScheduledConsequence[] = [];
    engine.subscribe((ev) => {
      if (ev.type === 'consequence:triggered') {
        triggeredList.push(ev.consequence);
      }
    });

    // Подбираем seed/выбор с высокой вероятностью отложенного последствия
    const res = engine.chooseDilemmaOption('sketchy_energy_drink', 'accept_unmarked');
    if (res.scheduledConsequence) {
      expect(engine.clock.pendingCount).toBe(1);
      engine.advanceDays(60);
      expect(triggeredList.length).toBe(1);
      expect(engine.clock.pendingCount).toBe(0);
    }
  });

  it('повышает ностальгию у олдов при резкой смене ниши канала', () => {
    const engine = new SimulEngine({
      seed: 555,
      originNiche: 'edutainment',
      initialPlatformAgents: 4_000,
    });

    // Формируем пул ветеранов
    for (let i = 0; i < 300; i++) {
      engine.audience.subscribed[i] = 1;
      engine.audience.segment[i] = ViewerSegment.Veteran;
      engine.audience.loyalty[i] = 0.8;
    }
    engine.audience.applyGlobalImpulse({ flatSubscriberDelta: 5_000 });

    const before = engine.getAudienceSummary().avgNostalgia;
    engine.publishAndSimulateFullVideo({
      ...makeStrongVideo('Резкий уход в челленджи'),
      niche: 'entertainment',
    });
    const after = engine.getAudienceSummary().avgNostalgia;

    expect(after).toBeGreaterThan(before);
  });

  it('сохраняет и восстанавливает снапшот без потери детерминизма', () => {
    const original = new SimulEngine({ seed: 314159, originNiche: 'tech' });
    original.publishAndSimulateFullVideo(makeStrongVideo('Ролик 1'));
    original.advanceDays(4);

    const snap = original.createSnapshot();

    const restored = new SimulEngine({ seed: 1 });
    restored.restoreSnapshot(snap);

    const nextOrig = original.publishAndSimulateFullVideo(makeStrongVideo('Ролик 2'));
    const nextRest = restored.publishAndSimulateFullVideo(makeStrongVideo('Ролик 2'));

    expect(nextRest.totalViews).toBe(nextOrig.totalViews);
    expect(restored.subscribers).toBe(original.subscribers);
    expect(restored.creator.cashUsd).toBe(original.creator.cashUsd);
  });

  it('просчитывает 48 часовых волн на 25 000 SoA-агентах быстрее 150 мс', () => {
    const engine = new SimulEngine({
      seed: 8888,
      audienceCapacity: 25_000,
      initialPlatformAgents: 25_000,
    });

    const start = performance.now();
    const record = engine.publishAndSimulateFullVideo(makeStrongVideo('Стресс-тест'));
    const elapsedMs = performance.now() - start;

    expect(record.hourlyTimeline.length).toBe(48);
    expect(elapsedMs).toBeLessThan(150);
  });
});
