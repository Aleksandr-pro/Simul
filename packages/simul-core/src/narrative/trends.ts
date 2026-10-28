// ============================================================================
// @simul/core/narrative/trends — Живые тренды платформы с ограниченным временем жизни
// ============================================================================

import type { SeededPrng } from '../math/prng.js';
import { NICHE_ORDER, type NicheId, type TrendCard, type TrendPhase } from '../types.js';

const TREND_TEMPLATES: Record<NicheId, readonly string[]> = {
  gaming: [
    'Спидран хардкорного инди-хита вслепую',
    'Выживание 100 дней на экстремальной карте',
    'Разбор секретного обновления главного шутера сезона',
    'Турнир стримеров без донатного снаряжения',
  ],
  tech: [
    'Честный краш-тест нового складного флагмана',
    'Сборка ультра-тихого ПК за минимальный бюджет',
    'Неделя жизни с локальной нейросетью вместо смартфона',
    'Разоблачение поддельных аксессуаров с маркетплейсов',
  ],
  lifestyle: [
    'Переезд в микро-студию и минимализм на 30 дней',
    'Подъём в 5 утра: реальный дневник энергии без прикрас',
    'Месяц жизни строго на средний бюджет подписчика',
    'Цифровой детокс без соцсетей на неделю',
  ],
  edutainment: [
    'Как алгоритмы лент управляют дофамином зрителя',
    'Экономика пузырей: почему хайп всегда лопается',
    'Физика невозможных трюков из вирусных роликов',
    'История одного технологического провала на миллиард',
  ],
  entertainment: [
    'Прятки в огромном павильоне с призовым фондом',
    'Угадай настоящего профессионала среди 5 актёров',
    '24 часа в самой необычной локации города',
    'Кулинарный баттл: блюдо шефа за $5 против $500',
  ],
};

export class TrendSystem {
  private trends: TrendCard[] = [];
  private nextId = 1;

  seedInitialTrends(prng: SeededPrng, currentDay: number): void {
    this.trends = [];
    for (let i = 0; i < 4; i++) {
      this.spawnTrend(prng, currentDay);
    }
  }

  spawnTrend(prng: SeededPrng, currentDay: number, forcedNiche?: NicheId): TrendCard {
    const niche = forcedNiche ?? prng.pick(NICHE_ORDER);
    const titleRu = prng.pick(TREND_TEMPLATES[niche]);
    const lifetimeDays = prng.intRange(8, 18);

    const card: TrendCard = {
      id: `trend-${this.nextId++}`,
      titleRu,
      niche,
      phase: 'emerging',
      viralityBoost: Number(prng.range(1.24, 1.42).toFixed(2)),
      controversyRisk: Number(prng.range(0.05, 0.28).toFixed(2)),
      createdDay: currentDay,
      expiresDay: currentDay + lifetimeDays,
    };

    this.trends.push(card);
    return card;
  }

  /** Обновляет фазы жизни трендов и генерирует свежие взамен угасших */
  advanceToDay(currentDay: number, prng: SeededPrng): void {
    const alive: TrendCard[] = [];
    for (const t of this.trends) {
      if (currentDay > t.expiresDay) {
        continue;
      }
      const totalSpan = Math.max(1, t.expiresDay - t.createdDay);
      const progress = (currentDay - t.createdDay) / totalSpan;

      let phase: TrendPhase;
      let boost: number;
      if (progress < 0.28) {
        phase = 'emerging';
        boost = 1.28;
      } else if (progress < 0.62) {
        phase = 'peaking';
        boost = 1.40;
      } else if (progress < 0.85) {
        phase = 'saturated';
        boost = 0.92; // Перегретый тренд уже приелся аудитории!
      } else {
        phase = 'fading';
        boost = 0.78;
      }

      alive.push({
        ...t,
        phase,
        viralityBoost: boost,
      });
    }

    this.trends = alive;
    while (this.trends.length < 4) {
      this.spawnTrend(prng, currentDay);
    }
  }

  getActiveTrends(): readonly TrendCard[] {
    return this.trends;
  }

  findById(id: string | null | undefined): TrendCard | undefined {
    if (!id) return undefined;
    return this.trends.find((t) => t.id === id);
  }

  serialize(): { trends: TrendCard[]; nextId: number } {
    return {
      trends: this.trends.map((t) => ({ ...t })),
      nextId: this.nextId,
    };
  }

  restore(snap: { trends: TrendCard[]; nextId: number }): void {
    this.trends = snap.trends.map((t) => ({ ...t }));
    this.nextId = snap.nextId;
  }
}
