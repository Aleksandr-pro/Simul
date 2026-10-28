// ============================================================================
// @simul/core/algorithm/recommender — Живой многоволновой алгоритм рекомендаций
// Реализует 5 волн показов, смену редакций алгоритма, A/B-тест превью,
// индекс доверия канала (шадоубан) и давление общего пула показов.
// ============================================================================

import { clamp, sigmoid } from '../math/curves.js';
import type { SeededPrng } from '../math/prng.js';
import {
  NICHE_SPECS,
  type AlgorithmRegime,
  type AlgorithmRegimeId,
  type NicheId,
} from '../types.js';

export const ALGORITHM_REGIMES: Record<AlgorithmRegimeId, AlgorithmRegime> = {
  balanced_v1: {
    id: 'balanced_v1',
    titleRu: 'Сбалансированный поток 1.0',
    publicHintRu:
      'Платформа равномерно ценит кликабельность превью и среднее удержание зрителя.',
    ctrWeight: 0.30,
    retentionWeight: 0.36,
    velocityWeight: 0.18,
    satisfactionWeight: 0.16,
    clickbaitPenaltyScale: 1.0,
    adSafetySensitivity: 1.0,
  },
  clickbait_rush: {
    id: 'clickbait_rush',
    titleRu: 'Битва за первый клик',
    publicHintRu:
      'Лента агрессивно продвигает яркие обложки с высоким CTR в первые часы.',
    ctrWeight: 0.44,
    retentionWeight: 0.26,
    velocityWeight: 0.18,
    satisfactionWeight: 0.12,
    clickbaitPenaltyScale: 0.65,
    adSafetySensitivity: 0.85,
  },
  deep_retention: {
    id: 'deep_retention',
    titleRu: 'Эпоха глубокого просмотра',
    publicHintRu:
      'Алгоритм режет охват за быстрый отвал зрителей и вознаграждает досмотры до конца.',
    ctrWeight: 0.20,
    retentionWeight: 0.48,
    velocityWeight: 0.14,
    satisfactionWeight: 0.18,
    clickbaitPenaltyScale: 1.45,
    adSafetySensitivity: 1.0,
  },
  community_spark: {
    id: 'community_spark',
    titleRu: 'Социальный резонанс',
    publicHintRu:
      'Комментарии, лайки и подписки на первых волнах дают максимальный буст в тренды.',
    ctrWeight: 0.24,
    retentionWeight: 0.28,
    velocityWeight: 0.34,
    satisfactionWeight: 0.14,
    clickbaitPenaltyScale: 1.05,
    adSafetySensitivity: 0.95,
  },
  ad_safe_guard: {
    id: 'ad_safe_guard',
    titleRu: 'Щит брендов (Ad-Safe)',
    publicHintRu:
      'Ужесточена модерация: сомнительная реклама и кликбейт стремительно роняют траст канала.',
    ctrWeight: 0.26,
    retentionWeight: 0.34,
    velocityWeight: 0.16,
    satisfactionWeight: 0.24,
    clickbaitPenaltyScale: 1.6,
    adSafetySensitivity: 1.75,
  },
};

export const WAVE_THRESHOLDS: readonly [number, number, number, number, number] = [
  0.0,  // Wave 0 всегда выдаётся стартовой тестовой группе
  0.44, // Порог перехода на Wave 1 (похожая аудитория ниши)
  0.56, // Порог перехода на Wave 2 (широкая лента ниши)
  0.67, // Порог перехода на Wave 3 (вкладка «В тренде»)
  0.78, // Порог перехода на Wave 4 (глобальный вирусный взрыв)
] as const;

export interface ViralScoreInput {
  niche: NicheId;
  waveIndex: 0 | 1 | 2 | 3 | 4;
  observedCtr: number;
  observedRetention: number;
  views: number;
  likes: number;
  dislikes: number;
  comments: number;
  subGains: number;
  clickbaitLevel: number;
  intrinsicQuality: number;
  sketchySponsorLevel: number;
  trendBoost: number;
  consistencyMultiplier: number;
  worldPoolMultiplier: number;
}

export interface AlgorithmSnapshot {
  regimeId: AlgorithmRegimeId;
  trustScore: number;
  lastPublishDay: number | null;
  consecutiveConsistentUploads: number;
}

export class RecommendationEngine {
  private regimeIdValue: AlgorithmRegimeId = 'balanced_v1';
  private trustScoreValue = 1.0; // [0.05 .. 1.15]
  private lastPublishDayValue: number | null = null;
  private streakValue = 0;

  get regime(): AlgorithmRegime {
    return ALGORITHM_REGIMES[this.regimeIdValue];
  }

  get regimeId(): AlgorithmRegimeId {
    return this.regimeIdValue;
  }

  get trustScore(): number {
    return this.trustScoreValue;
  }

  /** Шадоубан активен при падении индекса доверия ниже 0.35 */
  get isShadowbanned(): boolean {
    return this.trustScoreValue < 0.35;
  }

  setRegime(regimeId: AlgorithmRegimeId): void {
    this.regimeIdValue = regimeId;
  }

  /** Случайное обновление алгоритма (не повторяет текущую редакцию) */
  mutateRegime(prng: SeededPrng): AlgorithmRegime {
    const allIds = Object.keys(ALGORITHM_REGIMES) as AlgorithmRegimeId[];
    const candidates = allIds.filter((id) => id !== this.regimeIdValue);
    this.regimeIdValue = prng.pick(candidates);
    return this.regime;
  }

  adjustTrust(delta: number): boolean {
    const wasBanned = this.isShadowbanned;
    this.trustScoreValue = clamp(this.trustScoreValue + delta, 0.08, 1.15);
    return wasBanned !== this.isShadowbanned;
  }

  /** Вычисляет бонус/штраф регулярности публикаций и обновляет серию */
  registerUploadOnDay(currentDay: number): number {
    if (this.lastPublishDayValue === null) {
      this.lastPublishDayValue = currentDay;
      this.streakValue = 1;
      return 1.0;
    }

    const gapDays = Math.max(0, currentDay - this.lastPublishDayValue);
    this.lastPublishDayValue = currentDay;

    if (gapDays >= 2 && gapDays <= 7) {
      this.streakValue = Math.min(12, this.streakValue + 1);
      // Стабильность постепенно лечит траст канала
      this.adjustTrust(0.025);
      return clamp(1.0 + this.streakValue * 0.012, 1.0, 1.12);
    }

    if (gapDays < 2) {
      // Спам роликами чаще раза в 2 дня слегка рассеивает охват
      return 0.93;
    }

    if (gapDays > 16) {
      this.streakValue = 0;
      return 0.84;
    }

    this.streakValue = Math.max(1, this.streakValue - 1);
    return 0.97;
  }

  /** Прайм-тайм множитель по часу публикации (17:00–21:00 даёт лучший стартовый импульс) */
  computePublishHourMultiplier(hourOfDay: number): number {
    const h = ((hourOfDay % 24) + 24) % 24;
    if (h >= 17 && h <= 21) return 1.12;
    if (h >= 12 && h <= 16) return 1.04;
    if (h >= 2 && h <= 6) return 0.86;
    return 0.96;
  }

  /**
   * Рассчитывает композитный ViralScore [0..1.25] на основе фактических метрик ролика.
   */
  computeViralScore(input: ViralScoreInput): number {
    const reg = this.regime;
    const nicheSpec = NICHE_SPECS[input.niche];

    // С ростом волны аудитория становится «холоднее», поэтому эталонный CTR слегка снижается
    const waveCtrBench =
      nicheSpec.baselineCtr * (1 - input.waveIndex * 0.065);
    const waveRetBench =
      nicheSpec.baselineRetention * (1 - input.waveIndex * 0.035);

    const ctrScore = sigmoid((input.observedCtr - waveCtrBench) / 0.026);
    const retScore = clamp(input.observedRetention / Math.max(0.2, waveRetBench), 0, 1.28);

    const safeViews = Math.max(1, input.views);
    const engagementRate =
      (input.likes +
        input.comments * 2.5 +
        input.subGains * 4.0 -
        input.dislikes * 3.0) /
      safeViews;
    const velocityScore = sigmoid((engagementRate - 0.042) / 0.025);

    const expectationGap = input.intrinsicQuality - input.clickbaitLevel * 0.9;
    const clickbaitPenalty =
      expectationGap < -0.15
        ? Math.abs(expectationGap + 0.15) * 0.55 * reg.clickbaitPenaltyScale
        : 0;
    const sponsorPenalty =
      input.sketchySponsorLevel * 0.28 * reg.adSafetySensitivity;

    const satisfactionScore = clamp(
      0.55 * input.intrinsicQuality +
        0.45 * input.observedRetention -
        clickbaitPenalty -
        sponsorPenalty,
      0.05,
      1.15
    );

    const rawWeighted =
      reg.ctrWeight * ctrScore +
      reg.retentionWeight * retScore +
      reg.velocityWeight * velocityScore +
      reg.satisfactionWeight * satisfactionScore;

    const trustMultiplier = this.isShadowbanned
      ? 0.24
      : clamp(this.trustScoreValue, 0.35, 1.15);

    const finalScore =
      rawWeighted *
      trustMultiplier *
      input.consistencyMultiplier *
      input.trendBoost *
      input.worldPoolMultiplier;

    return clamp(finalScore, 0.02, 1.25);
  }

  /**
   * Определяет целевую волну и объём показов на конкретном часе после публикации (1..48).
   */
  planHourlyImpressions(opts: {
    hourAfterPublish: number; // 1..48
    subscribers: number;
    currentViralScore: number;
    publishHourMultiplier: number;
    worldPoolMultiplier: number;
    collabBonusImpressions: number;
    prng: SeededPrng;
  }): { waveIndex: 0 | 1 | 2 | 3 | 4; impressions: number } {
    const h = opts.hourAfterPublish;

    // Окно максимальной доступной волны по времени с момента релиза
    let maxEligibleWave: 0 | 1 | 2 | 3 | 4 = 0;
    if (h >= 35) maxEligibleWave = 4;
    else if (h >= 19) maxEligibleWave = 3;
    else if (h >= 9) maxEligibleWave = 2;
    else if (h >= 3) maxEligibleWave = 1;

    // При шадоубане видео не выпускается дальше Wave 0
    if (this.isShadowbanned) {
      maxEligibleWave = 0;
    }

    let unlockedWave: 0 | 1 | 2 | 3 | 4 = 0;
    for (let w = 1 as 1 | 2 | 3 | 4; w <= maxEligibleWave; w = (w + 1) as 1 | 2 | 3 | 4) {
      if (opts.currentViralScore >= WAVE_THRESHOLDS[w]!) {
        unlockedWave = w;
      } else {
        break;
      }
    }

    const subs = Math.max(0, opts.subscribers);
    const waveHourlyPools: readonly [number, number, number, number, number] = [
      95 + subs * 0.16 + opts.collabBonusImpressions * 0.25,
      340 + subs * 0.24 + opts.collabBonusImpressions * 0.35,
      1_450 + subs * 0.45 + opts.collabBonusImpressions * 0.20,
      7_800 + subs * 0.95,
      36_000 + subs * 2.1,
    ];

    let basePool = waveHourlyPools[unlockedWave]!;

    // Если ролик не пробил новую волну на поздних часах, органический хвост плавно затухает
    if (unlockedWave < maxEligibleWave) {
      const hoursSinceStall = Math.max(1, h - [0, 3, 9, 19, 35][unlockedWave]!);
      basePool *= Math.pow(0.86, hoursSinceStall);
    } else {
      // Внутри активной волны формируется естественный колокол разгона
      const waveBoost = 0.85 + Math.max(0, opts.currentViralScore - 0.45) * 1.45;
      basePool *= waveBoost;
    }

    if (h <= 4) {
      basePool *= opts.publishHourMultiplier;
    }

    if (this.isShadowbanned) {
      basePool *= 0.18;
    }

    const jitter = opts.prng.range(0.91, 1.09);
    const impressions = Math.max(
      8,
      Math.round(basePool * opts.worldPoolMultiplier * jitter)
    );

    return { waveIndex: unlockedWave, impressions };
  }

  serialize(): AlgorithmSnapshot {
    return {
      regimeId: this.regimeIdValue,
      trustScore: this.trustScoreValue,
      lastPublishDay: this.lastPublishDayValue,
      consecutiveConsistentUploads: this.streakValue,
    };
  }

  restore(snap: AlgorithmSnapshot): void {
    this.regimeIdValue = snap.regimeId;
    this.trustScoreValue = snap.trustScore;
    this.lastPublishDayValue = snap.lastPublishDay;
    this.streakValue = snap.consecutiveConsistentUploads;
  }
}
