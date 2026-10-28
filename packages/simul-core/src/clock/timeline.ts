// ============================================================================
// @simul/core/clock/timeline — Гибридные часы симуляции и очередь последствий
// ============================================================================

import type { ScheduledConsequence } from '../types.js';

export interface ClockSnapshot {
  totalHours: number;
  pendingConsequences: ScheduledConsequence[];
}

/**
 * Управляет игровым временем:
 * - абсолютный счётчик часов `totalHours` (день 1, 10:00 утра = час 10);
 * - мин-куча (binary min-heap) отложенных последствий со сложностью вставки/выборки O(log N).
 */
export class SimulationClock {
  private totalHoursValue: number;
  private heap: ScheduledConsequence[] = [];

  constructor(initialHours = 10) {
    this.totalHoursValue = Math.max(0, Math.floor(initialHours));
  }

  get totalHours(): number {
    return this.totalHoursValue;
  }

  /** Текущий игровой день (начиная с 1) */
  get currentDay(): number {
    return Math.floor(this.totalHoursValue / 24) + 1;
  }

  /** Час текущих суток (0..23) */
  get hourOfDay(): number {
    return this.totalHoursValue % 24;
  }

  get pendingCount(): number {
    return this.heap.length;
  }

  /** Планирует отложенное последствие на указанный абсолютный игровой час */
  schedule(consequence: ScheduledConsequence): void {
    this.heap.push(consequence);
    this.bubbleUp(this.heap.length - 1);
  }

  /** Продвигает время на `hours` часов и возвращает все сработавшие последствия в хронологическом порядке */
  advanceHours(hours: number): ScheduledConsequence[] {
    if (hours <= 0) return [];
    this.totalHoursValue += Math.floor(hours);
    const due: ScheduledConsequence[] = [];
    while (this.heap.length > 0 && this.heap[0]!.triggerAtHour <= this.totalHoursValue) {
      const next = this.popMin();
      if (next) due.push(next);
    }
    return due;
  }

  peekPending(): readonly ScheduledConsequence[] {
    return [...this.heap].sort((a, b) => a.triggerAtHour - b.triggerAtHour);
  }

  serialize(): ClockSnapshot {
    return {
      totalHours: this.totalHoursValue,
      pendingConsequences: this.peekPending().map((item) => ({ ...item })),
    };
  }

  restore(snapshot: ClockSnapshot): void {
    this.totalHoursValue = snapshot.totalHours;
    this.heap = [];
    for (const item of snapshot.pendingConsequences) {
      this.schedule({ ...item });
    }
  }

  private popMin(): ScheduledConsequence | undefined {
    if (this.heap.length === 0) return undefined;
    const min = this.heap[0];
    const last = this.heap.pop()!;
    if (this.heap.length > 0) {
      this.heap[0] = last;
      this.sinkDown(0);
    }
    return min;
  }

  private bubbleUp(index: number): void {
    let idx = index;
    while (idx > 0) {
      const parent = (idx - 1) >> 1;
      if (this.heap[parent]!.triggerAtHour <= this.heap[idx]!.triggerAtHour) {
        break;
      }
      const tmp = this.heap[parent]!;
      this.heap[parent] = this.heap[idx]!;
      this.heap[idx] = tmp;
      idx = parent;
    }
  }

  private sinkDown(index: number): void {
    let idx = index;
    const length = this.heap.length;
    while (true) {
      const left = (idx << 1) + 1;
      const right = left + 1;
      let smallest = idx;

      if (
        left < length &&
        this.heap[left]!.triggerAtHour < this.heap[smallest]!.triggerAtHour
      ) {
        smallest = left;
      }
      if (
        right < length &&
        this.heap[right]!.triggerAtHour < this.heap[smallest]!.triggerAtHour
      ) {
        smallest = right;
      }
      if (smallest === idx) break;

      const tmp = this.heap[idx]!;
      this.heap[idx] = this.heap[smallest]!;
      this.heap[smallest] = tmp;
      idx = smallest;
    }
  }
}
