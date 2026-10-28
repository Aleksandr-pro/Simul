// ============================================================================
// @simul/studio — Интерактивная лаборатория и дебаг-стенд движка Simul (v0.1.0)
// Демонстрирует работу @simul/core, @simul/renderer и @simul/cloud вживую.
// ============================================================================

import './styles.css';
import {
  NICHE_ORDER,
  NICHE_SPECS,
  SimulEngine,
  VIEWER_SEGMENT_LABELS_RU,
  ViewerSegment,
  evaluatePackagingVariant,
  type NicheId,
  type PublishedVideoRecord,
} from '@simul/core';
import {
  SimulSoundEngine,
  SpringValue,
  WaveTelemetryCanvas,
  formatCompactNumber,
} from '@simul/renderer';
import {
  createWorldSyncAdapter,
  type LeaderboardEntry,
  type WorldNichePoolState,
} from '@simul/cloud';

const worldAdapter = createWorldSyncAdapter({
  supabaseUrl: import.meta.env.VITE_SUPABASE_URL,
  supabasePublishableKey: import.meta.env.VITE_SUPABASE_PUBLISHABLE_KEY,
  seasonId: import.meta.env.VITE_SIMUL_SEASON_ID ?? 'season-1',
});

const engine = new SimulEngine({
  seed: 20261029,
  handle: '@viral_pioneer',
  displayName: 'ВИРАЛ Лаб',
  originNiche: 'tech',
  startingCashUsd: 800,
  audienceCapacity: 20_000,
  initialPlatformAgents: 12_000,
});

const sound = new SimulSoundEngine();
const animatedSubs = new SpringValue(0, { stiffness: 180, damping: 24 });
const animatedCash = new SpringValue(engine.creator.cashUsd, {
  stiffness: 180,
  damping: 24,
});

// Состояние формы конструктора видео
const formState = {
  titleA: 'Я проверил алгоритм рекомендаций на 10 000 зрителей',
  titleB: 'Секрет взрывного удержания: что скрывает лента?',
  useAbTest: true,
  niche: 'tech' as NicheId,
  trendId: '' as string,
  budgetUsd: 120,
  hookStrength: 0.78,
  pacingTightness: 0.74,
  payoffStrength: 0.76,
  visualContrastA: 0.76,
  curiosityGapA: 0.72,
  clickbaitA: 0.18,
  visualContrastB: 0.84,
  curiosityGapB: 0.80,
  clickbaitB: 0.22,
  sponsorType: 'none' as 'none' | 'clean' | 'sketchy',
};

let chartMode: 'views' | 'retention' = 'views';
let eventLogs: string[] = [
  'Движок Simul v0.1.0 инициализирован (12 000 живых SoA-агентов в памяти).',
];
let poolState: WorldNichePoolState | null = null;
let leaderboard: LeaderboardEntry[] = [];
let benchmarkReport: string | null = null;
let liveTimerId: number | null = null;

function pushLog(message: string): void {
  eventLogs = [`[День ${engine.clock.currentDay}, ${engine.clock.hourOfDay}:00] ${message}`, ...eventLogs].slice(0, 28);
}

engine.subscribe((ev) => {
  switch (ev.type) {
    case 'stage:upgraded':
      sound.playWaveBreakthrough(4);
      pushLog(`🎉 НОВЫЙ ЭТАП КАНАЛА: «${engine.stageDefinition.titleRu}»!`);
      break;
    case 'algorithm:mutated':
      sound.playAlertWarning();
      pushLog(`⚡ ОБНОВЛЕНИЕ АЛГОРИТМА: «${ev.current.titleRu}» — ${ev.current.publicHintRu}`);
      break;
    case 'consequence:triggered':
      sound.playAlertWarning();
      pushLog(`⏳ ОТЛОЖЕННОЕ ПОСЛЕДСТВИЕ: ${ev.consequence.headlineRu}`);
      break;
    case 'shadowban:changed':
      sound.playAlertWarning();
      pushLog(
        ev.active
          ? `🚨 ШАДОУБАН АКТИВИРОВАН (траст ${ev.trustScore.toFixed(2)})`
          : `✅ Шадоубан снят (траст восстановлен до ${ev.trustScore.toFixed(2)})`
      );
      break;
    case 'video:wave_hour':
      if (ev.point.hourAfterPublish > 1 && ev.point.waveIndex >= 2) {
        const prev =
          engine.publishedVideos[engine.publishedVideos.length - 1]
            ?.hourlyTimeline[ev.point.hourAfterPublish - 2];
        if (prev && ev.point.waveIndex > prev.waveIndex) {
          sound.playWaveBreakthrough(ev.point.waveIndex);
          pushLog(
            `🚀 Видео пробило Волну ${ev.point.waveIndex} (ViralScore ${ev.point.viralScore.toFixed(2)})!`
          );
        }
      }
      break;
    case 'video:completed':
      sound.playCashTick();
      pushLog(
        `📊 48ч релиза «${ev.video.title}» завершены: ${formatCompactNumber(ev.video.totalViews)} просм., Волна ${ev.video.maxWaveReached}.`
      );
      void syncWorldAfterVideo(ev.video);
      break;
  }
});

async function refreshWorldState(): Promise<void> {
  poolState = await worldAdapter.fetchNichePoolState(formState.niche);
  engine.setWorldPoolMultiplier(poolState.poolMultiplier);
  leaderboard = await worldAdapter.fetchLeaderboard(formState.niche, {
    handle: engine.creator.handle,
    displayName: engine.creator.displayName,
    stage: engine.stage,
    subscribers: engine.subscribers,
    totalViews: engine.totalViews,
  });
  renderApp();
}

async function syncWorldAfterVideo(video: PublishedVideoRecord): Promise<void> {
  poolState = await worldAdapter.submitVideoRelease({
    handle: engine.creator.handle,
    displayName: engine.creator.displayName,
    stage: engine.stage,
    subscribers: engine.subscribers,
    totalViews: engine.totalViews,
    inGameDay: engine.clock.currentDay,
    video,
    runSeed: engine.initialSeed,
  });
  engine.setWorldPoolMultiplier(poolState.poolMultiplier);
  if (poolState.recentViralHeadlineRu) {
    pushLog(`🌍 Мир: ${poolState.recentViralHeadlineRu}`);
  }
  leaderboard = await worldAdapter.fetchLeaderboard(formState.niche, {
    handle: engine.creator.handle,
    displayName: engine.creator.displayName,
    stage: engine.stage,
    subscribers: engine.subscribers,
    totalViews: engine.totalViews,
  });
  renderApp();
}

const appRoot = document.getElementById('app')!;
let waveCanvasRenderer: WaveTelemetryCanvas | null = null;

function buildReleaseInput() {
  const sponsorDeal =
    formState.sponsorType === 'clean'
      ? { brandName: 'TechNova Pro', payoutUsd: 450, sketchiness: 0.08 }
      : formState.sponsorType === 'sketchy'
        ? { brandName: 'MiracleCoin X', payoutUsd: 1_800, sketchiness: 0.85 }
        : null;

  return {
    title: formState.titleA,
    niche: formState.niche,
    trendId: formState.trendId || null,
    productionBudgetUsd: formState.budgetUsd,
    publishHourOfDay: 19,
    sponsorDeal,
    editing: {
      hookStrength: formState.hookStrength,
      pacingTightness: formState.pacingTightness,
      payoffStrength: formState.payoffStrength,
      adPlacementRatio: sponsorDeal ? 0.45 : null,
    },
    packaging: {
      variantA: {
        id: 'A' as const,
        title: formState.titleA,
        visualContrast: formState.visualContrastA,
        curiosityGap: formState.curiosityGapA,
        clickbaitLevel: formState.clickbaitA,
      },
      variantB: formState.useAbTest
        ? {
            id: 'B' as const,
            title: formState.titleB,
            visualContrast: formState.visualContrastB,
            curiosityGap: formState.curiosityGapB,
            clickbaitLevel: formState.clickbaitB,
          }
        : undefined,
    },
    delegatedToTeam: engine.creatorSystem.members.length > 0,
  };
}

function startAnimatedRelease(): void {
  if (engine.isLiveVideoRunning) return;
  try {
    sound.playTap();
    engine.beginVideoRelease(buildReleaseInput());
    renderApp();

    liveTimerId = window.setInterval(() => {
      const pt = engine.stepLiveVideoHour();
      animatedSubs.setTarget(engine.subscribers);
      animatedCash.setTarget(engine.creator.cashUsd);

      const latestVid = engine.publishedVideos[engine.publishedVideos.length - 1];
      if (waveCanvasRenderer && latestVid) {
        waveCanvasRenderer.setData(latestVid.hourlyTimeline, latestVid.retentionCurve);
      }

      if (!pt || !engine.isLiveVideoRunning) {
        if (liveTimerId !== null) {
          clearInterval(liveTimerId);
          liveTimerId = null;
        }
        renderApp();
      } else {
        updateDynamicCountersOnly();
      }
    }, 55);
  } catch (err) {
    alert((err as Error).message);
  }
}

function runInstantRelease(): void {
  if (engine.isLiveVideoRunning) return;
  try {
    sound.playTap();
    const vid = engine.publishAndSimulateFullVideo(buildReleaseInput());
    animatedSubs.setTarget(engine.subscribers);
    animatedCash.setTarget(engine.creator.cashUsd);
    if (waveCanvasRenderer) {
      waveCanvasRenderer.setData(vid.hourlyTimeline, vid.retentionCurve);
    }
    renderApp();
  } catch (err) {
    alert((err as Error).message);
  }
}

function runKernelBenchmark(): void {
  sound.playTap();
  const benchEngine = new SimulEngine({
    seed: 777,
    audienceCapacity: 25_000,
    initialPlatformAgents: 25_000,
  });

  const t0 = performance.now();
  const totalCycles = 12; // 12 видео * 48 часов = 576 волновых прогонов по 25 000 SoA-агентов!
  for (let i = 0; i < totalCycles; i++) {
    benchEngine.publishAndSimulateFullVideo({
      title: `Bench Video #${i + 1}`,
      niche: 'tech',
      productionBudgetUsd: 20,
      publishHourOfDay: 18,
      editing: {
        hookStrength: 0.82,
        pacingTightness: 0.78,
        payoffStrength: 0.8,
        adPlacementRatio: null,
      },
      packaging: {
        variantA: {
          id: 'A',
          title: 'Тестовый вирусный заголовок',
          visualContrast: 0.82,
          curiosityGap: 0.78,
          clickbaitLevel: 0.15,
        },
      },
    });
    benchEngine.advanceDays(3);
  }
  const elapsedMs = performance.now() - t0;
  const totalHoursSimulated = totalCycles * 48;
  const msPerWaveHour = elapsedMs / totalHoursSimulated;

  benchmarkReport = `12 полных релизов (576 часовых волн × 25 000 SoA-агентов) просчитаны за ${elapsedMs.toFixed(1)} мс (${msPerWaveHour.toFixed(3)} мс/тик • ~${Math.round(1000 / Math.max(0.01, msPerWaveHour)).toLocaleString('ru-RU')} тиков/сек). Итог симуляции: ${formatCompactNumber(benchEngine.subscribers)} подписчиков.`;
  renderApp();
}

function updateDynamicCountersOnly(): void {
  const latestVid = engine.publishedVideos[engine.publishedVideos.length - 1];
  const elViews = document.getElementById('live-views');
  const elCtr = document.getElementById('live-ctr');
  const elRet = document.getElementById('live-ret');
  const elWave = document.getElementById('live-wave');
  const elRev = document.getElementById('live-rev');
  const elNetSubs = document.getElementById('live-netsubs');

  if (latestVid) {
    if (elViews) elViews.textContent = formatCompactNumber(latestVid.totalViews);
    if (elCtr) elCtr.textContent = `${(latestVid.avgCtr * 100).toFixed(1)}%`;
    if (elRet) elRet.textContent = `${(latestVid.avgRetention * 100).toFixed(0)}%`;
    if (elWave)
      elWave.textContent = `Волна ${latestVid.maxWaveReached} (${latestVid.simulatedHours}/48ч)`;
    if (elRev)
      elRev.textContent = `$${(latestVid.adSenseRevenueUsd + latestVid.sponsorRevenueUsd).toFixed(1)}`;
    if (elNetSubs)
      elNetSubs.textContent = `${latestVid.netSubscribers >= 0 ? '+' : ''}${formatCompactNumber(latestVid.netSubscribers)}`;
  }
}

function renderApp(): void {
  const aud = engine.getAudienceSummary();
  const latestVid = engine.publishedVideos[engine.publishedVideos.length - 1] ?? null;
  const skills = engine.creatorSystem.getEffectiveSkills();
  const evalPreviewA = evaluatePackagingVariant(
    {
      id: 'A',
      title: formState.titleA,
      visualContrast: formState.visualContrastA,
      curiosityGap: formState.curiosityGapA,
      clickbaitLevel: formState.clickbaitA,
    },
    formState.niche,
    skills.analytics
  );
  const activeTrends = engine.getActiveTrends();
  const dilemmas = engine.getAvailableDilemmas();

  appRoot.innerHTML = `
    <div class="studio-shell">
      <header class="topbar">
        <div class="brand-block">
          <span class="engine-badge">SIMUL ENGINE v0.1</span>
          <div>
            <h1 class="brand-title">ВИРАЛ — Интерактивная лаборатория движка</h1>
            <p class="brand-sub">${engine.creator.displayName} (${engine.creator.handle}) • Этап: <strong>${engine.stageDefinition.titleRu}</strong></p>
          </div>
        </div>
        <div class="hud-pills">
          <div class="hud-pill">Подписчики: <strong id="hud-subs">${formatCompactNumber(engine.subscribers)}</strong></div>
          <div class="hud-pill">Бюджет: <strong id="hud-cash">$${engine.creator.cashUsd.toFixed(0)}</strong></div>
          <div class="hud-pill">Энергия: <strong>${Math.round(engine.creator.energy)}%</strong> • Выгорание: <strong>${Math.round(engine.creator.burnout)}%</strong></div>
          <div class="hud-pill ${engine.recommender.isShadowbanned ? 'pill-warn' : ''}">
            Алгоритм: <strong>${engine.recommender.regime.titleRu}</strong> • Траст: <strong>${engine.recommender.trustScore.toFixed(2)}</strong>
          </div>
          <div class="hud-pill">День <strong>${engine.clock.currentDay}</strong>, ${String(engine.clock.hourOfDay).padStart(2, '0')}:00</div>
          <div class="hud-pill">Мир: <strong>${worldAdapter.mode === 'supabase' ? 'Supabase Cloud' : 'Local + AI Rivals'}</strong> (×${engine.worldPoolMultiplier.toFixed(2)})</div>
          <button id="btn-mute">${sound.muted ? '🔇 Звук выкл' : '🔊 Звук вкл'}</button>
        </div>
      </header>

      <main class="workspace-grid">
        <!-- ЛЕВАЯ КОЛОНКА: Выпуск видео и мини-параметры -->
        <section>
          <div class="card">
            <h2>
              <span>🎬 Конвейер выпуска видео (1 ход)</span>
              <span style="font-size:11.5px;color:var(--accent)">Прогноз CTR: ${(evalPreviewA.predictedCtr * 100).toFixed(1)}%</span>
            </h2>

            <div class="field">
              <label><span>Ниша видео</span><span>CPM $${NICHE_SPECS[formState.niche].baseCpmUsd}</span></label>
              <select id="inp-niche">
                ${NICHE_ORDER.map(
                  (n) =>
                    `<option value="${n}" ${formState.niche === n ? 'selected' : ''}>${NICHE_SPECS[n].titleRu}</option>`
                ).join('')}
              </select>
            </div>

            <div class="field">
              <label><span>Активный тренд платформы</span></label>
              <select id="inp-trend">
                <option value="">— Авторская тема вне трендов (×1.00) —</option>
                ${activeTrends
                  .map(
                    (t) =>
                      `<option value="${t.id}" ${formState.trendId === t.id ? 'selected' : ''}>[${NICHE_SPECS[t.niche].titleRu} • ${t.phase}] ${t.titleRu} (×${t.viralityBoost})</option>`
                  )
                  .join('')}
              </select>
            </div>

            <div class="field">
              <label><span>Заголовок А (основной)</span></label>
              <input id="inp-title-a" type="text" value="${escapeHtml(formState.titleA)}" />
            </div>

            <div class="field">
              <label>
                <span><input id="inp-ab" type="checkbox" ${formState.useAbTest ? 'checked' : ''}/> A/B-тест второго заголовка и превью</span>
              </label>
              ${
                formState.useAbTest
                  ? `<input id="inp-title-b" type="text" value="${escapeHtml(formState.titleB)}" />`
                  : ''
              }
            </div>

            <div class="field">
              <label><span>Мини-игра «Монтаж»: Хук первых 5 сек</span><strong>${Math.round(formState.hookStrength * 100)}%</strong></label>
              <input id="inp-hook" type="range" min="0.15" max="1" step="0.01" value="${formState.hookStrength}" />
            </div>

            <div class="field">
              <label><span>Мини-игра «Монтаж»: Ритм и резка пауз</span><strong>${Math.round(formState.pacingTightness * 100)}%</strong></label>
              <input id="inp-pacing" type="range" min="0.15" max="1" step="0.01" value="${formState.pacingTightness}" />
            </div>

            <div class="field">
              <label><span>Мини-игра «Превью»: Контраст и интрига</span><strong>${Math.round(formState.visualContrastA * 100)}%</strong></label>
              <input id="inp-contrast" type="range" min="0.15" max="1" step="0.01" value="${formState.visualContrastA}" />
            </div>

            <div class="field">
              <label><span>Уровень кликбейта (риск отвала и потери траста!)</span><strong>${Math.round(formState.clickbaitA * 100)}%</strong></label>
              <input id="inp-clickbait" type="range" min="0" max="0.95" step="0.01" value="${formState.clickbaitA}" />
            </div>

            <div class="field">
              <label><span>Бюджет съёмки ($)</span><strong>$${formState.budgetUsd}</strong></label>
              <input id="inp-budget" type="range" min="0" max="500" step="20" value="${formState.budgetUsd}" />
            </div>

            <div class="field">
              <label><span>Рекламная интеграция</span></label>
              <select id="inp-sponsor">
                <option value="none" ${formState.sponsorType === 'none' ? 'selected' : ''}>Без спонсора (максимальное удержание)</option>
                <option value="clean" ${formState.sponsorType === 'clean' ? 'selected' : ''}>Проверенный бренд (+$450, низкий риск)</option>
                <option value="sketchy" ${formState.sponsorType === 'sketchy' ? 'selected' : ''}>Сомнительный токен (+$1 800, удар по трасту!)</option>
              </select>
            </div>

            <div class="btn-row">
              <button id="btn-publish-live" class="btn-primary" ${engine.isLiveVideoRunning ? 'disabled' : ''}>
                ▶ Живой релиз (48ч волны)
              </button>
              <button id="btn-publish-fast" ${engine.isLiveVideoRunning ? 'disabled' : ''}>
                ⚡ Мгновенно
              </button>
            </div>
          </div>

          <div class="card">
            <h2><span>🧭 Действия между роликами и команда</span></h2>
            <div class="btn-row" style="margin-bottom:10px">
              <button id="btn-rest" ${engine.isLiveVideoRunning ? 'disabled' : ''}>🛌 +4 дня отдыха</button>
              <button id="btn-stream" ${engine.isLiveVideoRunning ? 'disabled' : ''}>🔴 Провести стрим (2ч)</button>
              <button id="btn-algo" ${engine.isLiveVideoRunning ? 'disabled' : ''}>🎲 Сменить алгоритм</button>
              <button id="btn-upgrade" ${engine.isLiveVideoRunning ? 'disabled' : ''}>🎥 Апгрейд студии (Ур.${engine.creator.equipmentTier})</button>
              <button id="btn-hire" ${engine.isLiveVideoRunning ? 'disabled' : ''}>👥 Нанять монтажёра</button>
            </div>
            <div class="mini-stat-row"><span>Навыки (Сцен / Монт / Хар / Анал):</span><strong>${Math.round(skills.scriptwriting)} / ${Math.round(skills.editing)} / ${Math.round(skills.charisma)} / ${Math.round(skills.analytics)}</strong></div>
            <div class="mini-stat-row"><span>Сотрудников в штате:</span><strong>${engine.creatorSystem.members.length} / ${engine.stageDefinition.teamSlots}</strong></div>
            <div class="mini-stat-row"><span>Отложенных последствий в очереди:</span><strong>${engine.clock.pendingCount}</strong></div>
          </div>
        </section>

        <!-- ЦЕНТРАЛЬНАЯ КОЛОНКА: Живой Canvas2D рендер, пост-мортем и комментарии -->
        <section>
          <div class="card">
            <h2>
              <span>📈 Живая реакция алгоритма (@simul/renderer High-DPI Canvas)</span>
              <div class="btn-row">
                <button id="btn-mode-views" style="${chartMode === 'views' ? 'border-color:var(--accent)' : ''}">Волны 48ч</button>
                <button id="btn-mode-ret" style="${chartMode === 'retention' ? 'border-color:var(--accent)' : ''}">Удержание</button>
              </div>
            </h2>

            <div class="canvas-wrap">
              <canvas id="wave-canvas"></canvas>
            </div>

            <div class="kpi-grid">
              <div class="kpi-box"><span>Просмотры ролика</span><strong id="live-views">${latestVid ? formatCompactNumber(latestVid.totalViews) : '—'}</strong></div>
              <div class="kpi-box"><span>Средний CTR</span><strong id="live-ctr">${latestVid ? `${(latestVid.avgCtr * 100).toFixed(1)}%` : '—'}</strong></div>
              <div class="kpi-box"><span>Удержание (APV)</span><strong id="live-ret">${latestVid ? `${(latestVid.avgRetention * 100).toFixed(0)}%` : '—'}</strong></div>
              <div class="kpi-box"><span>Пиковая волна</span><strong id="live-wave">${latestVid ? `Волна ${latestVid.maxWaveReached} (${latestVid.simulatedHours}/48ч)` : '—'}</strong></div>
              <div class="kpi-box"><span>Доход с видео</span><strong id="live-rev">${latestVid ? `$${(latestVid.adSenseRevenueUsd + latestVid.sponsorRevenueUsd).toFixed(1)}` : '—'}</strong></div>
              <div class="kpi-box"><span>Прирост подписчиков</span><strong id="live-netsubs">${latestVid ? `${latestVid.netSubscribers >= 0 ? '+' : ''}${formatCompactNumber(latestVid.netSubscribers)}` : '—'}</strong></div>
            </div>

            ${
              latestVid?.postMortem
                ? `
              <div style="margin-top:10px">
                <div style="font-weight:700;font-size:13.5px;margin-bottom:4px">${latestVid.postMortem.verdictTitleRu}</div>
                <div style="font-size:12.5px;color:var(--muted);margin-bottom:10px">${latestVid.postMortem.summaryRu}</div>
                ${latestVid.postMortem.factors
                  .map(
                    (f) => `
                  <div class="factor-item factor-${f.kind}">
                    <strong>${f.labelRu}:</strong> ${f.detailRu}
                  </div>
                `
                  )
                  .join('')}
              </div>
            `
                : ''
            }
          </div>

          <div class="card">
            <h2><span>💬 Живой поток комментариев (по сегментам агентов)</span></h2>
            ${
              latestVid && latestVid.topComments.length > 0
                ? latestVid.topComments
                    .map(
                      (c) => `
                  <div class="comment-item">
                    <div class="comment-head">
                      <span><strong>${c.authorHandle}</strong> <span class="seg-tag">${VIEWER_SEGMENT_LABELS_RU[c.segment]}</span></span>
                      <span>❤️ ${formatCompactNumber(c.likes)}</span>
                    </div>
                    <div>${c.text}</div>
                  </div>
                `
                    )
                    .join('')
                : `<div style="font-size:12.5px;color:var(--muted)">Опубликуйте первое видео, чтобы агенты-зрители оставили комментарии.</div>`
            }
          </div>
        </section>

        <!-- ПРАВАЯ КОЛОНКА: SoA-пул агентов, Дилеммы, Лидерборд, Бенчмарк -->
        <section>
          <div class="card">
            <h2><span>🧠 Живая SoA-аудитория (${aud.activeAgents.toLocaleString('ru-RU')} агентов)</span></h2>
            <div class="mini-stat-row"><span>Средняя лояльность:</span><strong>${(aud.avgLoyalty * 100).toFixed(1)}%</strong></div>
            <div class="mini-stat-row"><span>Усталость от контента:</span><strong>${(aud.avgFatigue * 100).toFixed(1)}%</strong></div>
            <div class="mini-stat-row"><span>Ностальгия («раньше лучше»):</span><strong>${(aud.avgNostalgia * 100).toFixed(1)}%</strong></div>
            <div style="margin-top:8px">
              ${(
                [
                  ViewerSegment.Casual,
                  ViewerSegment.Subscriber,
                  ViewerSegment.CoreFan,
                  ViewerSegment.Veteran,
                  ViewerSegment.Critic,
                  ViewerSegment.Hater,
                  ViewerSegment.BrandScout,
                ] as const
              )
                .map(
                  (seg) => `
                <div class="mini-stat-row">
                  <span>${VIEWER_SEGMENT_LABELS_RU[seg]}</span>
                  <strong>${aud.segmentCounts[seg].toLocaleString('ru-RU')}</strong>
                </div>
              `
                )
                .join('')}
            </div>

            <div style="margin-top:12px" class="btn-row">
              <button id="btn-bench">⚡ Запустить стресс-бенчмарк ядра (25k агентов)</button>
              <button id="btn-save">💾 Сохранить снапшот</button>
            </div>
            ${benchmarkReport ? `<div class="bench-box">${benchmarkReport}</div>` : ''}
          </div>

          <div class="card">
            <h2><span>⚖️ Нарративные дилеммы</span></h2>
            ${
              dilemmas.length > 0
                ? (() => {
                    const d = dilemmas[0]!;
                    return `
                    <div style="font-weight:700;font-size:13px;margin-bottom:4px">${d.titleRu}</div>
                    <div style="font-size:12px;color:var(--muted);margin-bottom:10px">${d.situationRu}</div>
                    ${d.choices
                      .map(
                        (ch) => `
                      <button class="dilemma-btn" data-dilemma="${d.id}" data-choice="${ch.id}" style="width:100%;text-align:left;margin-bottom:7px">
                        <div>${ch.labelRu}</div>
                        <div style="font-size:11px;color:var(--muted);font-weight:400">${ch.immediatePreviewRu}</div>
                      </button>
                    `
                      )
                      .join('')}
                  `;
                  })()
                : `<div style="font-size:12px;color:var(--muted)">Достигните этапа «Растущий канал» (1 000 подписчиков), чтобы открыть первые крупные дилеммы индустрии!</div>`
            }
          </div>

          <div class="card">
            <h2><span>🏆 Рейтинг ниши «${NICHE_SPECS[formState.niche].titleRu}»</span></h2>
            ${leaderboard
              .slice(0, 6)
              .map(
                (entry) => `
              <div class="mini-stat-row" style="${entry.isPlayer ? 'color:var(--accent);font-weight:700' : ''}">
                <span>#${entry.rank} ${entry.displayName}</span>
                <strong>${formatCompactNumber(entry.subscribers)} подп.</strong>
              </div>
            `
              )
              .join('')}
          </div>

          <div class="card">
            <h2><span>📜 Журнал событий движка</span></h2>
            <div class="event-feed">
              ${eventLogs.map((l) => `<div class="event-line">${l}</div>`).join('')}
            </div>
          </div>
        </section>
      </main>
    </div>
  `;

  // Инициализация Canvas2D рендерера
  const canvasEl = document.getElementById('wave-canvas') as HTMLCanvasElement | null;
  if (canvasEl) {
    waveCanvasRenderer = new WaveTelemetryCanvas(canvasEl);
    waveCanvasRenderer.setMode(chartMode);
    if (latestVid) {
      waveCanvasRenderer.setData(latestVid.hourlyTimeline, latestVid.retentionCurve);
    }
    waveCanvasRenderer.render(0.016);
  }

  bindDomEvents();
}

function bindDomEvents(): void {
  document.getElementById('btn-mute')?.addEventListener('click', () => {
    sound.muted = !sound.muted;
    renderApp();
  });

  document.getElementById('inp-niche')?.addEventListener('change', (e) => {
    formState.niche = (e.target as HTMLSelectElement).value as NicheId;
    void refreshWorldState();
  });

  document.getElementById('inp-trend')?.addEventListener('change', (e) => {
    formState.trendId = (e.target as HTMLSelectElement).value;
    renderApp();
  });

  document.getElementById('inp-title-a')?.addEventListener('input', (e) => {
    formState.titleA = (e.target as HTMLInputElement).value;
  });

  document.getElementById('inp-title-b')?.addEventListener('input', (e) => {
    formState.titleB = (e.target as HTMLInputElement).value;
  });

  document.getElementById('inp-ab')?.addEventListener('change', (e) => {
    formState.useAbTest = (e.target as HTMLInputElement).checked;
    renderApp();
  });

  const bindRange = (
    id: string,
    assign: (val: number) => void
  ): void => {
    document.getElementById(id)?.addEventListener('change', (e) => {
      assign(Number((e.target as HTMLInputElement).value));
      renderApp();
    });
  };

  bindRange('inp-hook', (v) => {
    formState.hookStrength = v;
  });
  bindRange('inp-pacing', (v) => {
    formState.pacingTightness = v;
  });
  bindRange('inp-contrast', (v) => {
    formState.visualContrastA = v;
    formState.curiosityGapA = v;
  });
  bindRange('inp-clickbait', (v) => {
    formState.clickbaitA = v;
  });
  bindRange('inp-budget', (v) => {
    formState.budgetUsd = v;
  });

  document.getElementById('inp-sponsor')?.addEventListener('change', (e) => {
    formState.sponsorType = (e.target as HTMLSelectElement).value as
      | 'none'
      | 'clean'
      | 'sketchy';
    renderApp();
  });

  document
    .getElementById('btn-publish-live')
    ?.addEventListener('click', startAnimatedRelease);
  document
    .getElementById('btn-publish-fast')
    ?.addEventListener('click', runInstantRelease);

  document.getElementById('btn-mode-views')?.addEventListener('click', () => {
    chartMode = 'views';
    renderApp();
  });
  document.getElementById('btn-mode-ret')?.addEventListener('click', () => {
    chartMode = 'retention';
    renderApp();
  });

  document.getElementById('btn-rest')?.addEventListener('click', () => {
    sound.playTap();
    engine.advanceDays(4);
    pushLog('Вы отдохнули 4 дня: энергия восстановлена, выгорание снижено.');
    renderApp();
  });

  document.getElementById('btn-stream')?.addEventListener('click', () => {
    sound.playCashTick();
    const res = engine.runLiveStream({
      durationHours: 2,
      chatResponsiveness: 0.82,
      moderationAccuracy: 0.85,
      hypeMoments: 0.78,
    });
    pushLog(
      `🔴 Стрим завершён: пик ${formatCompactNumber(res.peakConcurrentViewers)} зрителей, донаты +$${res.donationsUsd}, +${res.newSubscribers} подп.`
    );
    renderApp();
  });

  document.getElementById('btn-algo')?.addEventListener('click', () => {
    engine.triggerAlgorithmUpdate();
    renderApp();
  });

  document.getElementById('btn-upgrade')?.addEventListener('click', () => {
    try {
      const up = engine.upgradeStudioEquipment();
      sound.playCashTick();
      pushLog(`🎥 Оборудование студии улучшено до уровня ${up.newTier} (-$${up.costUsd}).`);
      renderApp();
    } catch (err) {
      alert((err as Error).message);
    }
  });

  document.getElementById('btn-hire')?.addEventListener('click', () => {
    try {
      const m = engine.hireTeamMember('editor', 65);
      sound.playCashTick();
      pushLog(`👥 В команду принят ${m.nameRu} ($${m.monthlySalaryUsd}/мес).`);
      renderApp();
    } catch (err) {
      alert((err as Error).message);
    }
  });

  document.getElementById('btn-bench')?.addEventListener('click', runKernelBenchmark);

  document.getElementById('btn-save')?.addEventListener('click', () => {
    void (async () => {
      const snap = engine.createSnapshot();
      await worldAdapter.saveSnapshot('slot-1', snap);
      sound.playCashTick();
      pushLog('💾 Полный детерминированный снапшот движка сохранён.');
      renderApp();
    })();
  });

  document.querySelectorAll<HTMLButtonElement>('.dilemma-btn').forEach((btn) => {
    btn.addEventListener('click', () => {
      const dilemmaId = btn.dataset.dilemma!;
      const choiceId = btn.dataset.choice!;
      const res = engine.chooseDilemmaOption(dilemmaId, choiceId);
      sound.playTap();
      pushLog(
        res.scheduledConsequence
          ? '⚖️ Решение принято. Последствие вернётся через несколько недель...'
          : '⚖️ Решение по дилемме принято.'
      );
      renderApp();
    });
  });
}

function escapeHtml(text: string): string {
  return text
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;');
}

// 60 FPS цикл пружинной физики счётчиков и частиц Canvas
let prevFrameTime = performance.now();
function rafLoop(now: number): void {
  const dt = (now - prevFrameTime) / 1000;
  prevFrameTime = now;

  if (!animatedSubs.isSettled) {
    const v = animatedSubs.step(dt);
    const el = document.getElementById('hud-subs');
    if (el) el.textContent = formatCompactNumber(Math.round(v));
  }
  if (!animatedCash.isSettled) {
    const c = animatedCash.step(dt);
    const el = document.getElementById('hud-cash');
    if (el) el.textContent = `$${Math.round(c)}`;
  }
  if (waveCanvasRenderer?.hasActiveParticles) {
    waveCanvasRenderer.render(dt);
  }

  requestAnimationFrame(rafLoop);
}

requestAnimationFrame(rafLoop);
void refreshWorldState();
