// ============================================================================
// @simul/core/narrative/dilemmas — Дилеммы с отложенными на недели/месяцы последствиями
// ============================================================================

import type { SeededPrng } from '../math/prng.js';
import type {
  ChannelStage,
  DilemmaChoice,
  DilemmaDefinition,
  ScheduledConsequence,
} from '../types.js';

const STAGE_RANK: Record<ChannelStage, number> = {
  novice: 0,
  growing: 1,
  known: 2,
  star: 3,
  empire: 4,
};

export const DILEMMA_CATALOG: readonly DilemmaDefinition[] = [
  {
    id: 'sketchy_energy_drink',
    minStage: 'growing',
    titleRu: 'Золотой оффер от «НейроБуст Энерджи»',
    situationRu:
      'Малоизвестный бренд энергетиков предлагает тройной гонорар за нативную интеграцию без пометки рекламы. Состав напитка вызывает вопросы у врачей.',
    choices: [
      {
        id: 'accept_unmarked',
        labelRu: 'Взять $3 200 и похвалить как свой любимый напиток',
        immediatePreviewRu: 'Мгновенно +$3 200 на студию, но высокий риск отложенного скандала.',
        immediateCashUsd: 3_200,
        immediateEnergyDelta: 0,
        immediateReputationDelta: -4,
        immediateTrustDelta: -0.05,
        delayedConsequence: {
          delayDaysMin: 24,
          delayDaysMax: 48,
          probability: 0.78,
          headlineRu: 'Разоблачение «НейроБуст Энерджи» ударило по блогерам',
          descriptionRu:
            'Независимая экспертиза нашла нарушения у спонсора. Зрители припомнили вашу интеграцию без маркировки — доверие алгоритма и фанатов просело.',
          trustDelta: -0.24,
          reputationDelta: -18,
          cashDeltaUsd: -600,
          loyaltyDelta: -0.18,
          nostalgiaDelta: 0.05,
          burnoutDelta: 10,
        },
      },
      {
        id: 'decline_honestly',
        labelRu: 'Отказаться и рассказать подписчикам про честный фильтр рекламы',
        immediatePreviewRu: '$0 сейчас, но рост репутации и уважения «ветеранов».',
        immediateCashUsd: 0,
        immediateEnergyDelta: -2,
        immediateReputationDelta: 7,
        immediateTrustDelta: 0.04,
        delayedConsequence: {
          delayDaysMin: 18,
          delayDaysMax: 35,
          probability: 0.65,
          headlineRu: 'Крупный белый бренд заметил вашу принципиальность',
          descriptionRu:
            'После краха «НейроБуста» маркетологи надёжного техно-бренда вышли на вас с чистым спонсорским грантом.',
          trustDelta: 0.05,
          reputationDelta: 8,
          cashDeltaUsd: 2_100,
          loyaltyDelta: 0.09,
          nostalgiaDelta: 0,
          burnoutDelta: -4,
        },
      },
    ],
  },
  {
    id: 'drama_expose_rival',
    minStage: 'growing',
    titleRu: 'Слив переписки конкурента по нише',
    situationRu:
      'В руки попал неоднозначный скриншот закрытого чата популярного блогера. Острое видео гарантирует взрывной трафик, но вырвано из контекста.',
    choices: [
      {
        id: 'publish_drama',
        labelRu: 'Снять громкое разоблачение сегодня вечером',
        immediatePreviewRu: 'Быстрый хайп и деньги с просмотров, но рост токсичности.',
        immediateCashUsd: 1_400,
        immediateEnergyDelta: -12,
        immediateReputationDelta: -8,
        immediateTrustDelta: -0.04,
        delayedConsequence: {
          delayDaysMin: 20,
          delayDaysMax: 40,
          probability: 0.72,
          headlineRu: 'Ответный ролик с полным контекстом переписки',
          descriptionRu:
            'Оппонент опубликовал полную запись разговора. Часть аудитории разочаровалась в подаче и перешла в стан критиков.',
          trustDelta: -0.14,
          reputationDelta: -15,
          cashDeltaUsd: 0,
          loyaltyDelta: -0.15,
          nostalgiaDelta: 0.08,
          burnoutDelta: 14,
        },
      },
      {
        id: 'ignore_drama',
        labelRu: 'Не лезть в грязь и сфокусироваться на своём флагманском ролике',
        immediatePreviewRu: 'Спокойный темп, сохранение энергии и чистого имени.',
        immediateCashUsd: 0,
        immediateEnergyDelta: 5,
        immediateReputationDelta: 4,
        immediateTrustDelta: 0.02,
      },
    ],
  },
  {
    id: 'exclusive_agency_contract',
    minStage: 'known',
    titleRu: 'Эксклюзивный контракт с медиа-холдингом «Орион»',
    situationRu:
      'Крупное агентство предлагает $18 000 авансом в обмен на право согласовывать темы ваших роликов ближайшие три месяца.',
    choices: [
      {
        id: 'sign_agency',
        labelRu: 'Подписать контракт и забрать $18 000 на апгрейд студии',
        immediatePreviewRu: '+$18 000 сразу, но риск потери авторского почерка.',
        immediateCashUsd: 18_000,
        immediateEnergyDelta: -5,
        immediateReputationDelta: -3,
        immediateTrustDelta: 0,
        delayedConsequence: {
          delayDaysMin: 35,
          delayDaysMax: 65,
          probability: 0.8,
          headlineRu: 'Олды жалуются на «стерильный продюсерский пластик»',
          descriptionRu:
            'Навязанные холдингом интеграции утомили старых фанатов: волна комментариев «раньше было лучше» и рост выгорания.',
          trustDelta: -0.04,
          reputationDelta: -9,
          cashDeltaUsd: 0,
          loyaltyDelta: -0.12,
          nostalgiaDelta: 0.25,
          burnoutDelta: 18,
        },
      },
      {
        id: 'stay_indie',
        labelRu: 'Остаться независимым автором',
        immediatePreviewRu: 'Меньше денег сейчас, но полная творческая свобода и лояльность ядра.',
        immediateCashUsd: 0,
        immediateEnergyDelta: 4,
        immediateReputationDelta: 6,
        immediateTrustDelta: 0.03,
        delayedConsequence: {
          delayDaysMin: 25,
          delayDaysMax: 50,
          probability: 0.6,
          headlineRu: 'Сообщество поддержало независимый спецпроект донатами',
          descriptionRu:
            'Ядро фанатов организовало флешмоб поддержки независимого канала.',
          trustDelta: 0.04,
          reputationDelta: 9,
          cashDeltaUsd: 4_500,
          loyaltyDelta: 0.14,
          nostalgiaDelta: -0.08,
          burnoutDelta: -6,
        },
      },
    ],
  },
];

export class DilemmaEngine {
  private resolvedIds = new Set<string>();
  private nextConsequenceSeq = 1;

  getAvailableDilemmas(currentStage: ChannelStage): DilemmaDefinition[] {
    const rank = STAGE_RANK[currentStage];
    return DILEMMA_CATALOG.filter(
      (d) => STAGE_RANK[d.minStage] <= rank && !this.resolvedIds.has(d.id)
    );
  }

  resolveChoice(opts: {
    dilemmaId: string;
    choiceId: string;
    currentTotalHours: number;
    prng: SeededPrng;
  }): {
    choice: DilemmaChoice;
    scheduledConsequence: ScheduledConsequence | null;
  } {
    const dilemma = DILEMMA_CATALOG.find((d) => d.id === opts.dilemmaId);
    if (!dilemma) {
      throw new Error(`Unknown dilemma: ${opts.dilemmaId}`);
    }
    const choice = dilemma.choices.find((c) => c.id === opts.choiceId);
    if (!choice) {
      throw new Error(`Unknown choice ${opts.choiceId} for dilemma ${opts.dilemmaId}`);
    }

    this.resolvedIds.add(dilemma.id);

    let scheduledConsequence: ScheduledConsequence | null = null;
    if (
      choice.delayedConsequence &&
      opts.prng.chance(choice.delayedConsequence.probability)
    ) {
      const delayDays = opts.prng.intRange(
        choice.delayedConsequence.delayDaysMin,
        choice.delayedConsequence.delayDaysMax
      );
      scheduledConsequence = {
        id: `conseq-${this.nextConsequenceSeq++}`,
        sourceDilemmaId: dilemma.id,
        triggerAtHour: opts.currentTotalHours + delayDays * 24,
        headlineRu: choice.delayedConsequence.headlineRu,
        descriptionRu: choice.delayedConsequence.descriptionRu,
        trustDelta: choice.delayedConsequence.trustDelta,
        reputationDelta: choice.delayedConsequence.reputationDelta,
        cashDeltaUsd: choice.delayedConsequence.cashDeltaUsd,
        loyaltyDelta: choice.delayedConsequence.loyaltyDelta,
        nostalgiaDelta: choice.delayedConsequence.nostalgiaDelta,
        burnoutDelta: choice.delayedConsequence.burnoutDelta,
      };
    }

    return { choice, scheduledConsequence };
  }

  serialize(): { resolvedIds: string[]; nextConsequenceSeq: number } {
    return {
      resolvedIds: Array.from(this.resolvedIds),
      nextConsequenceSeq: this.nextConsequenceSeq,
    };
  }

  restore(snap: { resolvedIds: string[]; nextConsequenceSeq: number }): void {
    this.resolvedIds = new Set(snap.resolvedIds);
    this.nextConsequenceSeq = snap.nextConsequenceSeq;
  }
}
