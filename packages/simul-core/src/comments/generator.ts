// ============================================================================
// @simul/core/comments/generator — Гибридный генератор живых комментариев
// Сочетает мгновенный процедурный синтез по сегментам и метрикам видео (0$ / 0мс)
// с расширяемым интерфейсом LLMCommentEnricher для сюжетных и вирусных пиков.
// ============================================================================

import type { SeededPrng } from '../math/prng.js';
import {
  ViewerSegment,
  type GeneratedComment,
  type NicheId,
  type PublishedVideoRecord,
} from '../types.js';

export interface LLMCommentEnricher {
  generateBatch(context: {
    videoTitle: string;
    niche: NicheId;
    maxWave: number;
    avgRetention: number;
    isNicheShifted: boolean;
  }): Promise<GeneratedComment[]>;
}

const HANDLES_BY_SEGMENT: Record<ViewerSegment, readonly string[]> = {
  [ViewerSegment.Casual]: ['@pixel_fox', '@night_tea', '@maks_scroll', '@lena_wave', '@dan_random'],
  [ViewerSegment.Subscriber]: ['@sub_nikita', '@katya_watches', '@artem_live', '@dasha_bell'],
  [ViewerSegment.CoreFan]: ['@fan_first_sec', '@team_viral_top', '@oleg_always_here', '@kira_stan'],
  [ViewerSegment.Veteran]: ['@old_since_100subs', '@lampoviy_2024', '@ded_kanala', '@veteran_vibe'],
  [ViewerSegment.Critic]: ['@montage_inspector', '@frame_by_frame', '@logic_check', '@cinema_nerd'],
  [ViewerSegment.Hater]: ['@dislike_patrol', '@skip_sponsor', '@toxic_truth', '@unsub_soon'],
  [ViewerSegment.BrandScout]: ['@brand_partners_ru', '@media_buyer_pro', '@agency_scout'],
};

export class HybridCommentGenerator {
  private enricher: LLMCommentEnricher | null = null;
  private seq = 1;

  setLLMEnricher(enricher: LLMCommentEnricher | null): void {
    this.enricher = enricher;
  }

  get hasLLMEnricher(): boolean {
    return this.enricher !== null;
  }

  generateForVideo(opts: {
    video: PublishedVideoRecord;
    isNicheShifted: boolean;
    hasSketchySponsor: boolean;
    clickbaitGap: number;
    weakestDecileIndex: number;
    prng: SeededPrng;
    count?: number;
  }): GeneratedComment[] {
    const count = opts.count ?? 6;
    const out: GeneratedComment[] = [];
    const { video, prng } = opts;

    const candidateSegments: ViewerSegment[] = [
      ViewerSegment.CoreFan,
      ViewerSegment.Subscriber,
      ViewerSegment.Casual,
      ViewerSegment.Critic,
      ViewerSegment.Veteran,
      ViewerSegment.Hater,
    ];
    if (video.maxWaveReached >= 2) {
      candidateSegments.push(ViewerSegment.BrandScout);
    }

    for (let i = 0; i < count; i++) {
      const seg = candidateSegments[i % candidateSegments.length] ?? prng.pick(candidateSegments);
      const authorHandle = prng.pick(HANDLES_BY_SEGMENT[seg]);
      const { text, sentiment } = this.composeText({
        segment: seg,
        video,
        isNicheShifted: opts.isNicheShifted,
        hasSketchySponsor: opts.hasSketchySponsor,
        clickbaitGap: opts.clickbaitGap,
        weakestDecileIndex: opts.weakestDecileIndex,
        prng,
      });

      const baseLikes = Math.max(
        1,
        Math.round((video.totalViews / 320) * prng.range(0.15, 1.45))
      );

      out.push({
        id: `cmt-${this.seq++}`,
        authorHandle,
        segment: seg,
        text,
        likes: baseLikes,
        sentiment,
        isPinned: i === 0 && seg === ViewerSegment.CoreFan,
      });
    }

    return out.sort((a, b) => b.likes - a.likes);
  }

  private composeText(opts: {
    segment: ViewerSegment;
    video: PublishedVideoRecord;
    isNicheShifted: boolean;
    hasSketchySponsor: boolean;
    clickbaitGap: number;
    weakestDecileIndex: number;
    prng: SeededPrng;
  }): { text: string; sentiment: GeneratedComment['sentiment'] } {
    const { segment, video, prng } = opts;
    const dropMinute = Math.max(1, opts.weakestDecileIndex + 1);

    switch (segment) {
      case ViewerSegment.CoreFan:
        if (video.maxWaveReached >= 3) {
          return {
            text: prng.pick([
              'Я здесь ещё до миллиона просмотров! Алгоритм наконец-то дал охваты, заслуженно!',
              'Лайк не глядя на первой минуте, монтаж в этом выпуске просто космос 🔥',
              'Вижу в трендах! Помню, как тут было по 200 просмотров, гордость берёт.',
            ]),
            sentiment: 'positive',
          };
        }
        return {
          text: prng.pick([
            'Как всегда держишь планку! Пишу коммент для продвижения ролика в рекомендации!',
            'Уведомление пришло — всё бросил и включил. Концовка мощная!',
          ]),
          sentiment: 'positive',
        };

      case ViewerSegment.Veteran:
        if (opts.isNicheShifted) {
          return {
            text: prng.pick([
              'Эх, раньше на канале была ламповая атмосфера, а теперь гонка за чужими трендами...',
              'Смотрю с первых выпусков, но новая тематика вообще не моя. Скучаю по старому формату.',
            ]),
            sentiment: 'negative',
          };
        }
        return {
          text: prng.pick([
            'Олды на месте! Приятно видеть, как вырос звук и свет, но вайб остался тем самым.',
            'Смотрю с первой тысячи подписчиков — рад, что не скатился в пустой кликбейт.',
          ]),
          sentiment: 'positive',
        };

      case ViewerSegment.Critic:
        if (video.avgRetention < 0.45) {
          return {
            text: `Идея интересная, но в районе ${dropMinute}-й минуты темп сильно провисает — рука сама потянулась перемотать.`,
            sentiment: 'neutral',
          };
        }
        return {
          text: prng.pick([
            'Респект за плотный монтаж без воды. Хук зацепил буквально с первых пяти секунд.',
            'Структура сценария выстроена грамотно: каждую треть ролика есть микро-кульминация.',
          ]),
          sentiment: 'positive',
        };

      case ViewerSegment.Hater:
        if (opts.hasSketchySponsor) {
          return {
            text: 'Опять сомнительная интеграция ради быстрых денег? Серьёзно, кто в это ещё верит?',
            sentiment: 'toxic',
          };
        }
        if (opts.clickbaitGap > 0.25) {
          return {
            text: 'Обложка кричит про сенсацию, а внутри пересказ очевидного на десять минут. Дизлайк за кликбейт.',
            sentiment: 'toxic',
          };
        }
        return {
          text: prng.pick([
            'Почему это вообще попало мне в рекомендации? Тема заезжена вдоль и поперёк.',
            'Слишком много пафоса в заголовке, по факту — обычный проходной выпуск.',
          ]),
          sentiment: 'negative',
        };

      case ViewerSegment.BrandScout:
        return {
          text: 'Отличное удержание и живая аудитория! Проверьте рабочую почту — отправили предложение по спецпроекту.',
          sentiment: 'positive',
        };

      case ViewerSegment.Subscriber:
      case ViewerSegment.Casual:
      default:
        if (video.maxWaveReached >= 2) {
          return {
            text: prng.pick([
              'Случайно залетело из рекомендаций — залип до самого финала, подписался!',
              'Впервые вижу канал, подача супер живая. Пошёл смотреть прошлые ролики.',
            ]),
            sentiment: 'positive',
          };
        }
        return {
          text: prng.pick([
            'Фоном под чай зашло неплохо, жду продолжения темы.',
            'Коротко и по делу, спасибо за выпуск!',
          ]),
          sentiment: 'neutral',
        };
    }
  }
}
