// ============================================================================
// @simul/core — Базовые типы и доменная модель движка Simul («ВИРАЛ»)
// ============================================================================

export type NicheId =
  | 'gaming'
  | 'tech'
  | 'lifestyle'
  | 'edutainment'
  | 'entertainment';

export const NICHE_ORDER: readonly NicheId[] = [
  'gaming',
  'tech',
  'lifestyle',
  'edutainment',
  'entertainment',
] as const;

export interface NicheSpec {
  id: NicheId;
  index: number;
  titleRu: string;
  /** Базовый доход на 1 000 монетизируемых просмотров ($) */
  baseCpmUsd: number;
  /** Эталонный CTR ниши на стартовой волне */
  baselineCtr: number;
  /** Эталонное среднее удержание (APV, 0..1) */
  baselineRetention: number;
  /** Скорость выгорания аудитории от однотипных тем */
  fatigueRate: number;
}

export const NICHE_SPECS: Record<NicheId, NicheSpec> = {
  gaming: {
    id: 'gaming',
    index: 0,
    titleRu: 'Гейминг и стримы',
    baseCpmUsd: 2.2,
    baselineCtr: 0.068,
    baselineRetention: 0.48,
    fatigueRate: 0.045,
  },
  tech: {
    id: 'tech',
    index: 1,
    titleRu: 'Техно и гаджеты',
    baseCpmUsd: 6.4,
    baselineCtr: 0.058,
    baselineRetention: 0.52,
    fatigueRate: 0.032,
  },
  lifestyle: {
    id: 'lifestyle',
    index: 2,
    titleRu: 'Лайфстайл и влоги',
    baseCpmUsd: 3.4,
    baselineCtr: 0.062,
    baselineRetention: 0.45,
    fatigueRate: 0.038,
  },
  edutainment: {
    id: 'edutainment',
    index: 3,
    titleRu: 'Научпоп и разборы',
    baseCpmUsd: 5.1,
    baselineCtr: 0.055,
    baselineRetention: 0.56,
    fatigueRate: 0.025,
  },
  entertainment: {
    id: 'entertainment',
    index: 4,
    titleRu: 'Шоу и челленджи',
    baseCpmUsd: 3.9,
    baselineCtr: 0.082,
    baselineRetention: 0.44,
    fatigueRate: 0.055,
  },
};

/** Сегменты «живой» аудитории (хранятся в Uint8Array пула агентов) */
export const enum ViewerSegment {
  Casual = 0,
  Subscriber = 1,
  CoreFan = 2,
  Veteran = 3,
  Critic = 4,
  Hater = 5,
  BrandScout = 6,
}

export const VIEWER_SEGMENT_LABELS_RU: Record<ViewerSegment, string> = {
  [ViewerSegment.Casual]: 'Случайные зрители',
  [ViewerSegment.Subscriber]: 'Подписчики',
  [ViewerSegment.CoreFan]: 'Ядро фанатов',
  [ViewerSegment.Veteran]: 'Олды («ветераны»)',
  [ViewerSegment.Critic]: 'Критики',
  [ViewerSegment.Hater]: 'Хейтеры',
  [ViewerSegment.BrandScout]: 'Скауты брендов',
};

/** 5 этапов прогрессии канала (раздел 5 концепт-документа) */
export type ChannelStage = 'novice' | 'growing' | 'known' | 'star' | 'empire';

export interface StageDefinition {
  id: ChannelStage;
  titleRu: string;
  minSubscribers: number;
  maxSubscribers: number | null;
  monetizationUnlocked: boolean;
  teamSlots: number;
  sponsorshipMultiplier: number;
  unlocksRu: string[];
}

export const STAGE_DEFINITIONS: readonly StageDefinition[] = [
  {
    id: 'novice',
    titleRu: 'Новичок',
    minSubscribers: 0,
    maxSubscribers: 1_000,
    monetizationUnlocked: false,
    teamSlots: 0,
    sponsorshipMultiplier: 0.0,
    unlocksRu: ['Базовая камера', 'Одна платформа', 'Простые видео'],
  },
  {
    id: 'growing',
    titleRu: 'Растущий канал',
    minSubscribers: 1_000,
    maxSubscribers: 50_000,
    monetizationUnlocked: true,
    teamSlots: 2,
    sponsorshipMultiplier: 1.0,
    unlocksRu: ['Партнёрская монетизация', 'Первые рекламные интеграции', 'Найм помощника'],
  },
  {
    id: 'known',
    titleRu: 'Известный блогер',
    minSubscribers: 50_000,
    maxSubscribers: 500_000,
    monetizationUnlocked: true,
    teamSlots: 4,
    sponsorshipMultiplier: 2.6,
    unlocksRu: ['Полноценная команда', 'Своя студия', 'Коллаборации', 'Сложные дилеммы'],
  },
  {
    id: 'star',
    titleRu: 'Звезда',
    minSubscribers: 500_000,
    maxSubscribers: 5_000_000,
    monetizationUnlocked: true,
    teamSlots: 6,
    sponsorshipMultiplier: 6.5,
    unlocksRu: ['Крупные контракты брендов', 'Мультиплатформа', 'Собственные шоу-проекты'],
  },
  {
    id: 'empire',
    titleRu: 'Империя',
    minSubscribers: 5_000_000,
    maxSubscribers: null,
    monetizationUnlocked: true,
    teamSlots: 10,
    sponsorshipMultiplier: 15.0,
    unlocksRu: ['Собственный медиа-бизнес', 'Прямое влияние на тренды индустрии'],
  },
];

/** Эпохи / редакции живого алгоритма рекомендаций */
export type AlgorithmRegimeId =
  | 'balanced_v1'
  | 'clickbait_rush'
  | 'deep_retention'
  | 'community_spark'
  | 'ad_safe_guard';

export interface AlgorithmRegime {
  id: AlgorithmRegimeId;
  titleRu: string;
  publicHintRu: string;
  ctrWeight: number;
  retentionWeight: number;
  velocityWeight: number;
  satisfactionWeight: number;
  clickbaitPenaltyScale: number;
  adSafetySensitivity: number;
}

/** Жизненный цикл тренда */
export type TrendPhase = 'emerging' | 'peaking' | 'saturated' | 'fading';

export interface TrendCard {
  id: string;
  titleRu: string;
  niche: NicheId;
  phase: TrendPhase;
  /** Множитель интереса алгоритма и CTR (например, 1.38 на пике, 0.82 при перенасыщении) */
  viralityBoost: number;
  /** Риск скандала или усталости аудитории (0..1) */
  controversyRisk: number;
  createdDay: number;
  expiresDay: number;
}

/** Параметры мини-игры «Монтаж» */
export interface EditingMiniGameInput {
  /** Сила хука первых 5 секунд (0..1) */
  hookStrength: number;
  /** Точность ритма и обрезки пустых пауз (0..1) */
  pacingTightness: number;
  /** Выразительность кульминации в финальной трети (0..1) */
  payoffStrength: number;
  /** Позиция рекламной интеграции (0..1 по хронометражу, null если без рекламы) */
  adPlacementRatio: number | null;
}

export interface EditingMiniGameResult {
  overallEditScore: number;
  /** 10 точек удержания по децилям хронометража (10%..100%) */
  retentionCurve: number[];
  /** Индекс главного «провала», где уходит больше всего зрителей (0..9) */
  weakestDecileIndex: number;
}

/** Вариант упаковки (заголовок + превью) для A/B-тестирования */
export interface PackagingVariant {
  id: 'A' | 'B';
  title: string;
  /** Визуальный контраст и читаемость превью на смартфоне (0..1) */
  visualContrast: number;
  /** Любопытство / интрига («гэп любопытства», 0..1) */
  curiosityGap: number;
  /** Уровень кликбейта и завышения ожиданий (0..1) */
  clickbaitLevel: number;
}

export interface PackagingMiniGameInput {
  variantA: PackagingVariant;
  variantB?: PackagingVariant;
}

/** План выпуска одного видео (основной игровой ход) */
export interface VideoReleaseInput {
  title: string;
  niche: NicheId;
  trendId?: string | null;
  /** Бюджет на продакшн и реквизит ($) */
  productionBudgetUsd: number;
  /** Оптимальный час публикации (0..23; прайм-тайм повышает стартовый импульс) */
  publishHourOfDay: number;
  /** Наличие спонсорской интеграции и её гонорар */
  sponsorDeal?: {
    brandName: string;
    payoutUsd: number;
    /** Насколько сомнительный продукт (0 = честный бренд, 1 = откровенный скам) */
    sketchiness: number;
  } | null;
  /** Коллаборация с другим каналом (даёт кросс-охват и множитель пула) */
  collabPartner?: {
    handle: string;
    subscribers: number;
    synergy: number;
  } | null;
  editing: EditingMiniGameInput;
  packaging: PackagingMiniGameInput;
  /** Делегировать ли черновой монтаж и сценарий команде (экономит энергию игрока) */
  delegatedToTeam?: boolean;
}

/** Часовая точка телеметрии живого релиза (первые 48 часов) */
export interface VideoWavePoint {
  hourAfterPublish: number;
  waveIndex: 0 | 1 | 2 | 3 | 4;
  impressions: number;
  views: number;
  cumulativeViews: number;
  ctr: number;
  avgWatchPct: number;
  likes: number;
  dislikes: number;
  comments: number;
  subGains: number;
  subLosses: number;
  viralScore: number;
  activeVariant: 'A' | 'B';
}

export interface PostMortemFactor {
  kind: 'positive' | 'negative' | 'neutral';
  labelRu: string;
  detailRu: string;
  impactScore: number;
}

export interface VideoPostMortem {
  verdictTitleRu: string;
  summaryRu: string;
  maxWaveReached: 0 | 1 | 2 | 3 | 4;
  winningVariant: 'A' | 'B';
  abTestLiftPct: number;
  factors: PostMortemFactor[];
}

export interface GeneratedComment {
  id: string;
  authorHandle: string;
  segment: ViewerSegment;
  text: string;
  likes: number;
  sentiment: 'positive' | 'neutral' | 'negative' | 'toxic';
  isPinned?: boolean;
}

export interface PublishedVideoRecord {
  id: string;
  sequenceNumber: number;
  title: string;
  niche: NicheId;
  publishedDay: number;
  publishedHour: number;
  isLiveSimulating: boolean;
  simulatedHours: number;
  maxWaveReached: 0 | 1 | 2 | 3 | 4;
  totalImpressions: number;
  totalViews: number;
  avgCtr: number;
  avgRetention: number;
  likes: number;
  dislikes: number;
  commentsCount: number;
  netSubscribers: number;
  adSenseRevenueUsd: number;
  sponsorRevenueUsd: number;
  productionCostUsd: number;
  retentionCurve: number[];
  hourlyTimeline: VideoWavePoint[];
  topComments: GeneratedComment[];
  postMortem: VideoPostMortem | null;
}

/** Навыки и личное состояние автора */
export interface CreatorSkills {
  scriptwriting: number; // 1..100
  editing: number;       // 1..100
  charisma: number;      // 1..100
  analytics: number;     // 1..100
}

export interface CreatorState {
  handle: string;
  displayName: string;
  originNiche: NicheId;
  currentNiche: NicheId;
  equipmentTier: number;       // 1..5
  energy: number;              // 0..100
  burnout: number;             // 0..100
  personalLifeBalance: number; // 0..100
  reputation: number;          // 0..100
  cashUsd: number;
  skills: CreatorSkills;
}

export type TeamRole = 'editor' | 'scriptwriter' | 'cameraman' | 'manager';

export interface TeamMember {
  id: string;
  nameRu: string;
  role: TeamRole;
  skill: number;        // 1..100
  morale: number;       // 0..100
  monthlySalaryUsd: number;
  quitRisk: number;     // 0..1
}

/** Отложенное последствие решения игрока */
export interface ScheduledConsequence {
  id: string;
  sourceDilemmaId: string;
  triggerAtHour: number;
  headlineRu: string;
  descriptionRu: string;
  trustDelta: number;
  reputationDelta: number;
  cashDeltaUsd: number;
  loyaltyDelta: number;
  nostalgiaDelta: number;
  burnoutDelta: number;
}

export interface DilemmaChoice {
  id: string;
  labelRu: string;
  immediatePreviewRu: string;
  immediateCashUsd: number;
  immediateEnergyDelta: number;
  immediateReputationDelta: number;
  immediateTrustDelta: number;
  delayedConsequence?: Omit<ScheduledConsequence, 'id' | 'sourceDilemmaId' | 'triggerAtHour'> & {
    delayDaysMin: number;
    delayDaysMax: number;
    probability: number;
  };
}

export interface DilemmaDefinition {
  id: string;
  minStage: ChannelStage;
  titleRu: string;
  situationRu: string;
  choices: DilemmaChoice[];
}

export interface AudienceSummary {
  activeAgents: number;
  capacity: number;
  subscribers: number;
  avgLoyalty: number;
  avgFatigue: number;
  avgNostalgia: number;
  segmentCounts: Record<ViewerSegment, number>;
}

/** События шины SimulEngine */
export type SimulEvent =
  | { type: 'video:published'; video: PublishedVideoRecord }
  | { type: 'video:wave_hour'; videoId: string; point: VideoWavePoint }
  | { type: 'video:completed'; video: PublishedVideoRecord }
  | { type: 'stage:upgraded'; previous: ChannelStage; current: ChannelStage }
  | { type: 'algorithm:mutated'; previous: AlgorithmRegimeId; current: AlgorithmRegime }
  | { type: 'consequence:triggered'; consequence: ScheduledConsequence }
  | { type: 'shadowban:changed'; active: boolean; trustScore: number }
  | { type: 'team:member_quit'; member: TeamMember }
  | { type: 'day:advanced'; day: number; hour: number };
