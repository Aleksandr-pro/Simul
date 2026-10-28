-- ============================================================================
-- Simul Engine — Миграция 0001: Асинхронный общий мир «ВИРАЛ» (Supabase / PG17)
-- Включает: сезоны, общий пул показов по нишам, каналы игроков, релизы видео,
-- облачные снапшоты сохранений, коллаборации, RLS-политики и атомарные RPC.
-- ============================================================================

create extension if not exists pgcrypto;

-- 1. Сезоны общего мира
create table if not exists public.simul_seasons (
  id text primary key,
  title text not null,
  algorithm_regime text not null default 'balanced_v1',
  starts_at timestamptz not null default now(),
  ends_at timestamptz,
  is_active boolean not null default true,
  created_at timestamptz not null default now()
);

-- 2. Общий пул показов и конкурентный накал по нишам
create table if not exists public.simul_niche_pools (
  season_id text not null references public.simul_seasons(id) on delete cascade,
  niche text not null check (niche in ('gaming', 'tech', 'lifestyle', 'edutainment', 'entertainment')),
  base_hourly_impressions bigint not null default 2500000,
  competition_heat numeric(6,4) not null default 1.0000 check (competition_heat >= 0.4000 and competition_heat <= 3.0000),
  viral_videos_24h integer not null default 0,
  updated_at timestamptz not null default now(),
  primary key (season_id, niche)
);

-- 3. Каналы игроков в общем мире
create table if not exists public.simul_channels (
  id uuid primary key default gen_random_uuid(),
  owner_id uuid not null references auth.users(id) on delete cascade,
  season_id text not null references public.simul_seasons(id) on delete cascade,
  handle text not null,
  display_name text not null,
  niche text not null check (niche in ('gaming', 'tech', 'lifestyle', 'edutainment', 'entertainment')),
  stage text not null default 'novice',
  subscribers bigint not null default 0 check (subscribers >= 0),
  total_views bigint not null default 0 check (total_views >= 0),
  trust_score numeric(5,4) not null default 1.0000,
  reputation numeric(6,2) not null default 50.00,
  in_game_day integer not null default 1,
  updated_at timestamptz not null default now(),
  unique (season_id, handle),
  unique (owner_id, season_id)
);

create index if not exists idx_simul_channels_leaderboard
  on public.simul_channels (season_id, niche, subscribers desc, total_views desc);

-- 4. Опубликованные видео в общем мире (влияют на пул показов ниши)
create table if not exists public.simul_video_releases (
  id uuid primary key default gen_random_uuid(),
  channel_id uuid not null references public.simul_channels(id) on delete cascade,
  season_id text not null references public.simul_seasons(id) on delete cascade,
  niche text not null check (niche in ('gaming', 'tech', 'lifestyle', 'edutainment', 'entertainment')),
  title text not null check (char_length(title) between 1 and 140),
  viral_score numeric(5,4) not null,
  max_wave smallint not null check (max_wave between 0 and 4),
  views bigint not null default 0,
  ctr numeric(5,4) not null default 0,
  retention numeric(5,4) not null default 0,
  run_seed bigint not null default 0,
  created_at timestamptz not null default now()
);

create index if not exists idx_simul_video_releases_recent
  on public.simul_video_releases (season_id, niche, created_at desc);

-- 5. Облачные сохранения (снапшоты состояния движка Simul)
create table if not exists public.simul_channel_snapshots (
  channel_id uuid primary key references public.simul_channels(id) on delete cascade,
  owner_id uuid not null references auth.users(id) on delete cascade,
  in_game_day integer not null default 1,
  in_game_hour integer not null default 0,
  engine_version text not null default '0.1.0',
  snapshot_payload jsonb not null,
  updated_at timestamptz not null default now()
);

-- 6. Асинхронные коллаборации между каналами игроков
create table if not exists public.simul_collab_offers (
  id uuid primary key default gen_random_uuid(),
  season_id text not null references public.simul_seasons(id) on delete cascade,
  from_channel_id uuid not null references public.simul_channels(id) on delete cascade,
  to_channel_id uuid not null references public.simul_channels(id) on delete cascade,
  niche text not null,
  synergy_bonus numeric(4,3) not null default 1.150,
  status text not null default 'pending' check (status in ('pending', 'accepted', 'declined', 'completed')),
  created_at timestamptz not null default now(),
  check (from_channel_id <> to_channel_id)
);

-- ============================================================================
-- Row-Level Security (RLS)
-- ============================================================================
alter table public.simul_seasons enable row level security;
alter table public.simul_niche_pools enable row level security;
alter table public.simul_channels enable row level security;
alter table public.simul_video_releases enable row level security;
alter table public.simul_channel_snapshots enable row level security;
alter table public.simul_collab_offers enable row level security;

-- Публичное чтение открытых таблиц мира и рейтингов
create policy "Public read seasons"
  on public.simul_seasons for select using (true);

create policy "Public read niche pools"
  on public.simul_niche_pools for select using (true);

create policy "Public read channels"
  on public.simul_channels for select using (true);

create policy "Public read video releases"
  on public.simul_video_releases for select using (true);

-- Владелец управляет только своим каналом
create policy "Owner insert channel"
  on public.simul_channels for insert
  to authenticated
  with check (auth.uid() = owner_id);

create policy "Owner update channel"
  on public.simul_channels for update
  to authenticated
  using (auth.uid() = owner_id)
  with check (auth.uid() = owner_id);

-- Снапшоты сохранений строго приватны для владельца
create policy "Owner select own snapshot"
  on public.simul_channel_snapshots for select
  to authenticated
  using (auth.uid() = owner_id);

create policy "Owner upsert own snapshot"
  on public.simul_channel_snapshots for insert
  to authenticated
  with check (auth.uid() = owner_id);

create policy "Owner update own snapshot"
  on public.simul_channel_snapshots for update
  to authenticated
  using (auth.uid() = owner_id)
  with check (auth.uid() = owner_id);

-- Коллаборации видны участникам
create policy "Participants view collabs"
  on public.simul_collab_offers for select
  to authenticated
  using (
    exists (
      select 1 from public.simul_channels c
      where (c.id = from_channel_id or c.id = to_channel_id)
        and c.owner_id = auth.uid()
    )
  );

-- ============================================================================
-- Атомарные функции общего пула алгоритма (PL/pgSQL RPC)
-- ============================================================================

-- Расчёт текущего множителя свободного пула показов ниши с учётом остывания накала
create or replace function public.claim_niche_pool_multiplier(
  p_season_id text,
  p_niche text
)
returns numeric
language plpgsql
security definer
set search_path = public
as $$
declare
  v_heat numeric;
  v_updated timestamptz;
  v_hours_elapsed numeric;
  v_cooled_heat numeric;
  v_multiplier numeric;
begin
  select competition_heat, updated_at
    into v_heat, v_updated
    from public.simul_niche_pools
   where season_id = p_season_id and niche = p_niche;

  if not found then
    return 1.0000;
  end if;

  v_hours_elapsed := greatest(0, extract(epoch from (now() - v_updated)) / 3600.0);
  -- Естественное затухание конкурентного перегрева к базовой 1.0 (8% в час)
  v_cooled_heat := 1.0 + (v_heat - 1.0) * power(0.92, least(v_hours_elapsed, 72));
  -- Чем выше перегрев (вирусные хиты конкурентов), тем сильнее сжимается свободный охват [0.58 .. 1.22]
  v_multiplier := least(1.22, greatest(0.58, round((1.0 / greatest(0.75, v_cooled_heat))::numeric, 4)));

  return v_multiplier;
end;
$$;

-- Публикация результата волны видео в общий мир и обновление накала ниши
create or replace function public.record_world_video_release(
  p_channel_id uuid,
  p_title text,
  p_viral_score numeric,
  p_max_wave smallint,
  p_views bigint,
  p_ctr numeric,
  p_retention numeric,
  p_subscribers bigint,
  p_total_views bigint,
  p_in_game_day integer,
  p_run_seed bigint default 0
)
returns jsonb
language plpgsql
security definer
set search_path = public
as $$
declare
  v_channel public.simul_channels%rowtype;
  v_heat_delta numeric := 0.0;
  v_new_heat numeric;
begin
  select * into v_channel
    from public.simul_channels
   where id = p_channel_id and owner_id = auth.uid();

  if not found then
    raise exception 'Channel not found or unauthorized';
  end if;

  insert into public.simul_video_releases (
    channel_id, season_id, niche, title, viral_score, max_wave, views, ctr, retention, run_seed
  ) values (
    v_channel.id, v_channel.season_id, v_channel.niche, p_title,
    p_viral_score, p_max_wave, p_views, p_ctr, p_retention, p_run_seed
  );

  update public.simul_channels
     set subscribers = greatest(0, p_subscribers),
         total_views = greatest(total_views, p_total_views),
         in_game_day = greatest(in_game_day, p_in_game_day),
         updated_at = now()
   where id = v_channel.id;

  -- Если ролик вышел в тренды (Wave 3) или стал мега-вирусным (Wave 4),
  -- он забирает внимание аудитории и повышает конкурентный накал ниши для соперников!
  if p_max_wave = 3 then
    v_heat_delta := 0.0650;
  elsif p_max_wave >= 4 then
    v_heat_delta := 0.1400;
  end if;

  update public.simul_niche_pools
     set competition_heat = least(2.4000, greatest(0.7500, competition_heat + v_heat_delta)),
         viral_videos_24h = viral_videos_24h + (case when p_max_wave >= 3 then 1 else 0 end),
         updated_at = now()
   where season_id = v_channel.season_id and niche = v_channel.niche
  returning competition_heat into v_new_heat;

  return jsonb_build_object(
    'ok', true,
    'season_id', v_channel.season_id,
    'niche', v_channel.niche,
    'new_competition_heat', coalesce(v_new_heat, 1.0)
  );
end;
$$;

-- Стартовые данные Сезона 1
insert into public.simul_seasons (id, title, algorithm_regime, is_active)
values ('season-1', 'Сезон 1: Зарождение алгоритма', 'balanced_v1', true)
on conflict (id) do nothing;

insert into public.simul_niche_pools (season_id, niche, base_hourly_impressions, competition_heat)
values
  ('season-1', 'gaming', 4200000, 1.0500),
  ('season-1', 'tech', 2100000, 0.9600),
  ('season-1', 'lifestyle', 3500000, 1.0000),
  ('season-1', 'edutainment', 1900000, 0.9200),
  ('season-1', 'entertainment', 5000000, 1.1200)
on conflict (season_id, niche) do nothing;
