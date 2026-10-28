// ============================================================================
// @simul/core/team/creator-team — Навыки, выгорание, этапы прогрессии и команда
// ============================================================================

import { clamp } from '../math/curves.js';
import type { SeededPrng } from '../math/prng.js';
import {
  STAGE_DEFINITIONS,
  type ChannelStage,
  type CreatorState,
  type NicheId,
  type StageDefinition,
  type TeamMember,
  type TeamRole,
} from '../types.js';

const CANDIDATE_NAMES_RU: Record<TeamRole, readonly string[]> = {
  editor: ['Илья (Монтажёр ритма)', 'Алина (Мастер динамики)', 'Тимур (Спец по VFX)'],
  scriptwriter: ['София (Сценарист хуков)', 'Марк (Нарративщик)', 'Вера (Шеф-редактор)'],
  cameraman: ['Денис (Оператор-постановщик)', 'Роман (Мастер света)'],
  manager: ['Екатерина (Коммерческий директор)', 'Павел (Продюсер брендов)'],
};

export function resolveStageForSubscribers(subscribers: number): StageDefinition {
  let current = STAGE_DEFINITIONS[0]!;
  for (const stage of STAGE_DEFINITIONS) {
    if (subscribers >= stage.minSubscribers) {
      current = stage;
    }
  }
  return current;
}

export class CreatorAndTeamSystem {
  public creator: CreatorState;
  private team: TeamMember[] = [];
  private nextMemberId = 1;
  private currentStageValue: ChannelStage = 'novice';

  constructor(opts: {
    handle: string;
    displayName: string;
    niche: NicheId;
    startingCashUsd?: number;
  }) {
    this.creator = {
      handle: opts.handle,
      displayName: opts.displayName,
      originNiche: opts.niche,
      currentNiche: opts.niche,
      equipmentTier: 1,
      energy: 100,
      burnout: 8,
      personalLifeBalance: 80,
      reputation: 50,
      cashUsd: opts.startingCashUsd ?? 450,
      skills: {
        scriptwriting: 18,
        editing: 16,
        charisma: 20,
        analytics: 14,
      },
    };
  }

  get currentStage(): ChannelStage {
    return this.currentStageValue;
  }

  get stageDefinition(): StageDefinition {
    return (
      STAGE_DEFINITIONS.find((s) => s.id === this.currentStageValue) ??
      STAGE_DEFINITIONS[0]!
    );
  }

  get members(): readonly TeamMember[] {
    return this.team;
  }

  /** Суммарные навыки с учётом усиления от нанятой команды */
  getEffectiveSkills() {
    let editBoost = 0;
    let scriptBoost = 0;
    let analyticsBoost = 0;

    for (const m of this.team) {
      const moraleFactor = 0.5 + (m.morale / 100) * 0.5;
      if (m.role === 'editor') editBoost += m.skill * 0.35 * moraleFactor;
      if (m.role === 'scriptwriter') scriptBoost += m.skill * 0.35 * moraleFactor;
      if (m.role === 'manager') analyticsBoost += m.skill * 0.30 * moraleFactor;
    }

    return {
      scriptwriting: clamp(this.creator.skills.scriptwriting + scriptBoost, 1, 100),
      editing: clamp(this.creator.skills.editing + editBoost, 1, 100),
      charisma: this.creator.skills.charisma,
      analytics: clamp(this.creator.skills.analytics + analyticsBoost, 1, 100),
    };
  }

  /** Обновляет этап прогрессии при росте подписчиков */
  syncStage(subscribers: number): { upgraded: boolean; previous: ChannelStage; current: ChannelStage } {
    const resolved = resolveStageForSubscribers(subscribers).id;
    const rankOrder: Record<ChannelStage, number> = {
      novice: 0,
      growing: 1,
      known: 2,
      star: 3,
      empire: 4,
    };
    const prev = this.currentStageValue;
    if (rankOrder[resolved] > rankOrder[prev]) {
      this.currentStageValue = resolved;
      return { upgraded: true, previous: prev, current: resolved };
    }
    return { upgraded: false, previous: prev, current: prev };
  }

  /** Найм сотрудника в команду */
  hireMember(role: TeamRole, skill: number, prng: SeededPrng): TeamMember {
    const maxSlots = this.stageDefinition.teamSlots;
    if (this.team.length >= maxSlots) {
      throw new Error(
        `Недостаточно слотов команды на этапе «${this.stageDefinition.titleRu}» (максимум ${maxSlots})`
      );
    }
    const clampedSkill = clamp(Math.round(skill), 15, 95);
    const monthlySalaryUsd = Math.round(350 + clampedSkill * 22);
    const member: TeamMember = {
      id: `staff-${this.nextMemberId++}`,
      nameRu: prng.pick(CANDIDATE_NAMES_RU[role]),
      role,
      skill: clampedSkill,
      morale: 82,
      monthlySalaryUsd,
      quitRisk: 0.04,
    };
    this.team.push(member);
    return member;
  }

  /** Апгрейд оборудования студии (уровни 1..5) */
  upgradeEquipment(): { newTier: number; costUsd: number } {
    if (this.creator.equipmentTier >= 5) {
      throw new Error('Оборудование студии уже максимального 5-го уровня');
    }
    const costs = [0, 650, 2_400, 9_500, 38_000] as const;
    const costUsd = costs[this.creator.equipmentTier] ?? 10_000;
    if (this.creator.cashUsd < costUsd) {
      throw new Error(`Недостаточно средств для апгрейда студии ($${costUsd})`);
    }
    this.creator.cashUsd -= costUsd;
    this.creator.equipmentTier += 1;
    return { newTier: this.creator.equipmentTier, costUsd };
  }

  /** Прокачка навыков автора и расход энергии при выпуске видео */
  applyVideoProductionEffort(delegatedToTeam: boolean): void {
    const hasStaff = delegatedToTeam && this.team.length > 0;
    const energyCost = hasStaff ? 16 : 34;

    if (this.creator.energy < energyCost) {
      // Работа на износе ускоряет выгорание!
      this.creator.burnout = clamp(this.creator.burnout + 12, 0, 100);
      this.creator.personalLifeBalance = clamp(
        this.creator.personalLifeBalance - 8,
        0,
        100
      );
    } else {
      this.creator.burnout = clamp(
        this.creator.burnout + (hasStaff ? 2.5 : 5.5),
        0,
        100
      );
    }

    this.creator.energy = clamp(this.creator.energy - energyCost, 0, 100);

    // Плавный рост мастерства с каждым отснятым роликом
    this.creator.skills.scriptwriting = clamp(
      Number((this.creator.skills.scriptwriting + 1.1).toFixed(2)),
      1,
      100
    );
    this.creator.skills.editing = clamp(
      Number((this.creator.skills.editing + (hasStaff ? 0.5 : 1.3)).toFixed(2)),
      1,
      100
    );
    this.creator.skills.charisma = clamp(
      Number((this.creator.skills.charisma + 0.9).toFixed(2)),
      1,
      100
    );
    this.creator.skills.analytics = clamp(
      Number((this.creator.skills.analytics + 1.0).toFixed(2)),
      1,
      100
    );
  }

  /** Восстановление за `days` межрелизных дней, списание зарплат и проверка ухода сотрудников */
  advanceDays(days: number, previousDay: number, nextDay: number, prng: SeededPrng): TeamMember[] {
    if (days <= 0) return [];

    this.creator.energy = clamp(this.creator.energy + days * 22, 0, 100);
    this.creator.burnout = clamp(this.creator.burnout - days * 2.2, 0, 100);
    this.creator.personalLifeBalance = clamp(
      this.creator.personalLifeBalance + days * 1.5,
      0,
      100
    );

    // Расчёт пересечения границы месяца (каждые 30 дней — день зарплаты команды)
    const monthsPassed = Math.floor(nextDay / 30) - Math.floor(previousDay / 30);
    const quitters: TeamMember[] = [];

    if (monthsPassed > 0 && this.team.length > 0) {
      const payroll =
        monthsPassed *
        this.team.reduce((acc, m) => acc + m.monthlySalaryUsd, 0);

      if (this.creator.cashUsd >= payroll) {
        this.creator.cashUsd -= payroll;
        for (const m of this.team) {
          m.morale = clamp(m.morale + 4, 0, 100);
          m.quitRisk = clamp((100 - m.morale) / 160, 0.01, 0.75);
        }
      } else {
        // Задержка зарплаты бьёт по морали и провоцирует увольнения!
        this.creator.cashUsd = Math.max(0, this.creator.cashUsd - payroll);
        for (const m of this.team) {
          m.morale = clamp(m.morale - 32, 0, 100);
          m.quitRisk = clamp((100 - m.morale) / 110, 0.05, 0.9);
        }
      }

      const staying: TeamMember[] = [];
      for (const m of this.team) {
        if (prng.chance(m.quitRisk) && m.morale < 55) {
          quitters.push(m);
        } else {
          staying.push(m);
        }
      }
      this.team = staying;
    }

    return quitters;
  }

  serialize() {
    return {
      creator: {
        ...this.creator,
        skills: { ...this.creator.skills },
      },
      team: this.team.map((m) => ({ ...m })),
      nextMemberId: this.nextMemberId,
      currentStage: this.currentStageValue,
    };
  }

  restore(snap: ReturnType<CreatorAndTeamSystem['serialize']>): void {
    this.creator = {
      ...snap.creator,
      skills: { ...snap.creator.skills },
    };
    this.team = snap.team.map((m) => ({ ...m }));
    this.nextMemberId = snap.nextMemberId;
    this.currentStageValue = snap.currentStage;
  }
}
