// ============================================================================
// @simul/core/content/minigames — Физические решатели мини-игр (Монтаж, CTR, Стрим)
// ============================================================================

import { clamp } from '../math/curves.js';
import {
  NICHE_SPECS,
  type CreatorSkills,
  type EditingMiniGameInput,
  type EditingMiniGameResult,
  type NicheId,
  type PackagingVariant,
} from '../types.js';

export interface PackagingEvaluation {
  appealScore: number;
  predictedCtr: number;
  clickbaitRisk: number;
}

export interface StreamMiniGameInput {
  durationHours: 1 | 2 | 3 | 4;
  /** Доля вовремя обработанных сообщений чата и донатов (0..1) */
  chatResponsiveness: number;
  /** Качество модерации троллей (0..1) */
  moderationAccuracy: number;
  /** Харизма и импровизация на эфире (0..1) */
  hypeMoments: number;
}

export interface StreamMiniGameResult {
  peakConcurrentViewers: number;
  donationsUsd: number;
  newSubscribers: number;
  energySpent: number;
  burnoutAdded: number;
}

/**
 * Решатель мини-игры «Монтаж»:
 * строит 10-точечный профиль вероятности удержания зрителя на каждом дециле видео.
 */
export function solveEditingMiniGame(
  input: EditingMiniGameInput,
  skills: CreatorSkills,
  equipmentTier: number,
  burnout: number
): EditingMiniGameResult {
  const editSkillBonus = (skills.editing / 100) * 0.20;
  const scriptSkillBonus = (skills.scriptwriting / 100) * 0.14;
  const equipBonus = ((equipmentTier - 1) / 4) * 0.10;
  const burnoutPenalty = burnout > 65 ? ((burnout - 65) / 35) * 0.22 : 0;

  const hook = clamp(input.hookStrength, 0, 1);
  const pacing = clamp(input.pacingTightness, 0, 1);
  const payoff = clamp(input.payoffStrength, 0, 1);

  const curve = new Array<number>(10);
  let weakestIdx = 0;
  let weakestVal = 2.0;

  const adDecile =
    input.adPlacementRatio !== null
      ? clamp(Math.floor(input.adPlacementRatio * 10), 0, 9)
      : -1;

  for (let d = 0; d < 10; d++) {
    let base: number;
    if (d === 0) {
      // Первые 10% ролика целиком зависят от хука!
      base = 0.42 + hook * 0.46 + editSkillBonus * 0.6;
    } else if (d <= 6) {
      // Середина держится на ритме монтажа и сценарии
      const midSag = d === 4 || d === 5 ? 0.05 : 0.02;
      base =
        0.40 +
        pacing * 0.40 +
        scriptSkillBonus +
        editSkillBonus * 0.5 -
        midSag;
    } else {
      // Финальная треть раскрывается кульминацией (payoff)
      base = 0.44 + payoff * 0.42 + scriptSkillBonus * 0.8;
    }

    // Если реклама поставлена в первые 20% видео — зрители массово закрывают ролик!
    if (d === adDecile) {
      base -= d <= 1 ? 0.22 : 0.08;
    }

    const finalStep = clamp(base + equipBonus - burnoutPenalty, 0.12, 0.97);
    curve[d] = Number(finalStep.toFixed(4));

    if (finalStep < weakestVal) {
      weakestVal = finalStep;
      weakestIdx = d;
    }
  }

  const overallEditScore = clamp(
    hook * 0.36 +
      pacing * 0.38 +
      payoff * 0.26 +
      editSkillBonus * 0.5 -
      burnoutPenalty,
    0.05,
    1.0
  );

  return {
    overallEditScore: Number(overallEditScore.toFixed(4)),
    retentionCurve: curve,
    weakestDecileIndex: weakestIdx,
  };
}

/**
 * Оценивает вариант обложки и заголовка и даёт прогноз CTR (как в п. 4.3 концепт-документа).
 */
export function evaluatePackagingVariant(
  variant: PackagingVariant,
  niche: NicheId,
  analyticsSkill: number
): PackagingEvaluation {
  const titleLen = variant.title.trim().length;
  // Оптимальная длина заголовка для мобильной ленты: 28..62 символа
  let lengthFactor = 1.0;
  if (titleLen < 12) lengthFactor = 0.76;
  else if (titleLen > 75) lengthFactor = 0.84;
  else if (titleLen >= 26 && titleLen <= 60) lengthFactor = 1.08;

  const contrast = clamp(variant.visualContrast, 0, 1);
  const curiosity = clamp(variant.curiosityGap, 0, 1);
  const clickbait = clamp(variant.clickbaitLevel, 0, 1);

  const skillEdge = (analyticsSkill / 100) * 0.10;

  const appealScore = clamp(
    (contrast * 0.42 + curiosity * 0.40 + clickbait * 0.24 + skillEdge) *
      lengthFactor,
    0.08,
    1.0
  );

  const baseCtr = NICHE_SPECS[niche].baselineCtr;
  const predictedCtr = clamp(baseCtr * (0.52 + 1.35 * appealScore), 0.012, 0.24);

  return {
    appealScore: Number(appealScore.toFixed(4)),
    predictedCtr: Number(predictedCtr.toFixed(4)),
    clickbaitRisk: clickbait,
  };
}

/**
 * Решатель мини-игры «Живой стрим» (п. 4.3 концепт-документа).
 */
export function solveStreamMiniGame(
  input: StreamMiniGameInput,
  subscribers: number,
  skills: CreatorSkills
): StreamMiniGameResult {
  const charismaFactor = 0.75 + (skills.charisma / 100) * 0.75;
  const quality = clamp(
    input.chatResponsiveness * 0.42 +
      input.hypeMoments * 0.38 +
      input.moderationAccuracy * 0.20,
    0.1,
    1.0
  );

  const baseConcurrency = Math.max(6, Math.round(12 + Math.pow(Math.max(1, subscribers), 0.68) * 0.55));
  const peakConcurrentViewers = Math.round(
    baseConcurrency * quality * charismaFactor * (0.85 + input.durationHours * 0.1)
  );

  const donationsUsd = Number(
    (
      peakConcurrentViewers *
      input.durationHours *
      0.16 *
      (0.5 + input.chatResponsiveness)
    ).toFixed(2)
  );

  const newSubscribers = Math.round(
    peakConcurrentViewers * 0.085 * quality * input.durationHours * 0.5
  );

  const energySpent = Math.min(85, input.durationHours * 16);
  const burnoutAdded = Number(
    (
      input.durationHours * 2.8 +
      (1 - input.moderationAccuracy) * 4.5
    ).toFixed(1)
  );

  return {
    peakConcurrentViewers,
    donationsUsd,
    newSubscribers,
    energySpent,
    burnoutAdded,
  };
}
