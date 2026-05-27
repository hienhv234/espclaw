/**
 * Wake word — 3 chế độ: library | keyword | train
 */

/** Chế độ nguồn wake word */
export type WakewordSourceMode = 'library' | 'keyword' | 'train';

export interface WakewordPreset {
  id: string;
  label: string;
  description: string;
  threshold: number;
  /** Thư viện Espressif / built-in (chỉ dùng cho mode library) */
  library: boolean;
  modelHint?: string;
}

/** Chỉ preset thuộc thư viện có sẵn — không gồm train tùy chỉnh */
export const WAKEWORD_LIBRARY: WakewordPreset[] = [
  {
    id: 'hi_esp',
    label: 'Hi ESP',
    description: 'WakeNet Espressif — tiếng Anh',
    threshold: 1200,
    library: true,
    modelHint: 'wakenet9_hi_esp',
  },
  {
    id: 'hey_claw',
    label: 'Hey Claw',
    description: 'Stub RMS — tiếng Anh',
    threshold: 1100,
    library: true,
  },
  {
    id: 'hi_lexin',
    label: 'Hi Lexin',
    description: 'WakeNet Espressif — tiếng Trung',
    threshold: 1250,
    library: true,
    modelHint: 'wakenet9_hi_lexin',
  },
  {
    id: 'nihao',
    label: '你好小智',
    description: 'WakeNet Espressif — tiếng Trung',
    threshold: 1300,
    library: true,
    modelHint: 'wakenet9_nihao',
  },
];

export function getLibraryPreset(id: string): WakewordPreset | undefined {
  return WAKEWORD_LIBRARY.find((p) => p.id === id);
}

/** Cấu hình sau khi backend xử lý — gửi xuống ESP */
export interface WakewordDeviceConfig {
  source_mode: WakewordSourceMode;
  enabled: boolean;
  threshold: number;
  label: string;
  preset?: string;
  keyword?: string;
  model_version?: number;
  train_job_id?: string;
}

export type WakewordMqttAction = 'sync' | 'enable';

export interface WakewordMqttPayload {
  type: 'wakeword';
  action: WakewordMqttAction;
  source_mode: WakewordSourceMode;
  enabled?: boolean;
  threshold?: number;
  preset?: string;
  keyword?: string;
  label?: string;
  model_version?: number;
  ts?: number;
}

export function buildWakewordSyncPayload(
  config: WakewordDeviceConfig
): WakewordMqttPayload {
  return {
    type: 'wakeword',
    action: 'sync',
    source_mode: config.source_mode,
    enabled: config.enabled,
    threshold: config.threshold,
    preset: config.preset,
    keyword: config.keyword,
    label: config.label,
    model_version: config.model_version,
    ts: Date.now(),
  };
}

/** Ngưỡng gợi ý từ từ khóa (stub — Phase 2: KWS model) */
export function suggestThresholdForKeyword(keyword: string): number {
  const len = keyword.trim().length;
  if (len <= 4) return 1400;
  if (len <= 10) return 1200;
  return 1100;
}
