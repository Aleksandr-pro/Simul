// ============================================================================
// @simul/core/content/diagnostics — Понятный пост-мортем разбор видео для игрока
// «После каждого цикла игрок получает понятный разбор: что сработало, что нет и почему»
// ============================================================================

import {
  NICHE_SPECS,
  type AlgorithmRegime,
  type PostMortemFactor,
  type PublishedVideoRecord,
  type VideoPostMortem,
} from '../types.js';

export function buildVideoPostMortem(opts: {
  video: PublishedVideoRecord;
  regime: AlgorithmRegime;
  isShadowbanned: boolean;
  clickbaitLevel: number;
  intrinsicQuality: number;
  weakestDecileIndex: number;
  winningVariant: 'A' | 'B';
  variantACtr: number;
  variantBCtr: number | null;
}): VideoPostMortem {
  const { video, regime } = opts;
  const nicheSpec = NICHE_SPECS[video.niche];
  const factors: PostMortemFactor[] = [];

  // 1. Оценка CTR относительно эталона ниши
  const ctrRatio = video.avgCtr / Math.max(0.01, nicheSpec.baselineCtr);
  if (ctrRatio >= 1.22) {
    factors.push({
      kind: 'positive',
      labelRu: `Сильная упаковка (CTR ${(video.avgCtr * 100).toFixed(1)}%)`,
      detailRu: `Обложка и заголовок пробили средний показатель ниши «${nicheSpec.titleRu}» (${(nicheSpec.baselineCtr * 100).toFixed(1)}%), ускорив проход первых волн.`,
      impactScore: 0.35,
    });
  } else if (ctrRatio < 0.85) {
    factors.push({
      kind: 'negative',
      labelRu: `Низкий CTR (${(video.avgCtr * 100).toFixed(1)}%)`,
      detailRu: `Зрители пролистывали карточку в ленте (норма ниши — ${(nicheSpec.baselineCtr * 100).toFixed(1)}%). Попробуйте усилить контраст превью или интригу заголовка.`,
      impactScore: -0.35,
    });
  } else {
    factors.push({
      kind: 'neutral',
      labelRu: `Рабочий CTR (${(video.avgCtr * 100).toFixed(1)}%)`,
      detailRu: `Кликабельность держится на уровне средних значений по нише.`,
      impactScore: 0.05,
    });
  }

  // 2. Анализ удержания и точки отвала
  const retRatio = video.avgRetention / Math.max(0.1, nicheSpec.baselineRetention);
  const dropStartPct = opts.weakestDecileIndex * 10;
  const dropEndPct = dropStartPct + 10;

  if (retRatio >= 1.12) {
    factors.push({
      kind: 'positive',
      labelRu: `Высокое удержание (${(video.avgRetention * 100).toFixed(0)}%)`,
      detailRu: `Плотность монтажа удержала внимание зрителей выше эталона платформы — это дало главный буст от алгоритма («${regime.titleRu}»).`,
      impactScore: 0.4,
    });
  } else if (retRatio < 0.86) {
    factors.push({
      kind: 'negative',
      labelRu: `Просадка удержания (${(video.avgRetention * 100).toFixed(0)}%)`,
      detailRu: `Массовый уход зрителей произошёл на отрезке ${dropStartPct}%–${dropEndPct}% хронометража. Алгоритм остановил расширение показов.`,
      impactScore: -0.42,
    });
  }

  // 3. Проверка обмана ожиданий (кликбейт)
  if (opts.clickbaitLevel - opts.intrinsicQuality > 0.25) {
    factors.push({
      kind: 'negative',
      labelRu: 'Штраф за кликбейт',
      detailRu:
        'Громкое обещание на превью не совпало с содержанием ролика: выросли дизлайки и просела лояльность ядра.',
      impactScore: -0.3,
    });
  }

  // 4. Результаты A/B-теста превью
  let abLift = 0;
  if (opts.variantBCtr !== null && opts.variantACtr > 0) {
    const best = Math.max(opts.variantACtr, opts.variantBCtr);
    const worst = Math.max(0.001, Math.min(opts.variantACtr, opts.variantBCtr));
    abLift = Number((((best - worst) / worst) * 100).toFixed(1));
    if (abLift >= 8) {
      factors.push({
        kind: 'positive',
        labelRu: `A/B-тест спас +${abLift}% кликов`,
        detailRu: `Алгоритм протестировал две обложки на старте и автоматически закрепил вариант ${opts.winningVariant}.`,
        impactScore: 0.2,
      });
    }
  }

  // 5. Шадоубан
  if (opts.isShadowbanned) {
    factors.push({
      kind: 'negative',
      labelRu: 'Ограничение охвата (Шадоубан)',
      detailRu:
        'Индекс доверия платформы к каналу критически низок. Показы были заблокированы за пределами узкой группы подписчиков.',
      impactScore: -0.8,
    });
  }

  const waveTitles: Record<0 | 1 | 2 | 3 | 4, string> = {
    0: 'Стагнация на тестовой группе (Волна 0)',
    1: 'Локальный охват внутри ниши (Волна 1)',
    2: 'Уверенный выход в рекомендации (Волна 2)',
    3: 'Прорыв во вкладку «В тренде»! (Волна 3)',
    4: 'МЕГА-ВИРАЛ на всю платформу! (Волна 4)',
  };

  const summaryByWave: Record<0 | 1 | 2 | 3 | 4, string> = {
    0: 'Видео осталось внутри узкого теста подписчиков: стартовых сигналов не хватило для второй волны.',
    1: 'Алгоритм показал ролик похожей аудитории ниши, но для широкой ленты не хватило связки CTR + досмотр.',
    2: 'Хороший рабочий выпуск: ролик уверенно собрал целевой трафик ниши и принёс новых подписчиков.',
    3: 'Отличный выстрел! Первые часы дали взрывной отклик, и алгоритм вывел видео на широкую аудиторию.',
    4: 'Исторический хит канала! Все метрики совпали с текущей редакцией алгоритма — видео захватило главную ленту.',
  };

  return {
    verdictTitleRu: waveTitles[video.maxWaveReached],
    summaryRu: summaryByWave[video.maxWaveReached],
    maxWaveReached: video.maxWaveReached,
    winningVariant: opts.winningVariant,
    abTestLiftPct: abLift,
    factors,
  };
}
