/**
 * Validation schemas for API inputs.
 * Pure TypeScript — no external dependencies.
 */

export interface ValidationError {
  field: string;
  message: string;
}

export interface ParseResult<T> {
  success: true;
  data: T;
  errors?: never;
}

export interface ParseFailure {
  success: false;
  data?: never;
  errors: ValidationError[];
}

export type SafeParseResult<T> = ParseResult<T> | ParseFailure;

function vError(field: string, message: string): ValidationError {
  return { field, message };
}

function makeError(field: string, message: string): ParseFailure {
  return { success: false, errors: [vError(field, message)] };
}

// ======================
// Device ID: GETAI-XXXXX (3-10 chars after hyphen)
// ======================

export function parseDeviceId(raw: unknown): SafeParseResult<string> {
  if (typeof raw !== 'string' || !raw.trim()) {
    return makeError('device_id', 'Device ID is required');
  }
  const val = raw.trim().toUpperCase();
  if (!/^GETAI-[A-Z0-9]{3,10}$/.test(val)) {
    return makeError(
      'device_id',
      'Device ID must be in format GETAI-XXXXX (e.g., GETAI-AB3K2) — 3 to 10 characters after the dash'
    );
  }
  return { success: true, data: val };
}

// ======================
// OTP: 6 digits
// ======================

export function parseOTP(raw: unknown): SafeParseResult<string> {
  if (typeof raw !== 'string') {
    return makeError('otp', 'OTP must be a string');
  }
  if (!/^\d{6}$/.test(raw)) {
    return makeError('otp', 'OTP must be exactly 6 digits');
  }
  return { success: true, data: raw };
}

// ======================
// Tenant ID: UUID
// ======================

const UUID_REGEX = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;

export function parseTenantId(raw: unknown): SafeParseResult<string> {
  if (typeof raw !== 'string' || !raw.trim()) {
    return makeError('tenant_id', 'Tenant ID is required');
  }
  if (!UUID_REGEX.test(raw.trim())) {
    return makeError('tenant_id', 'Invalid tenant ID format');
  }
  return { success: true, data: raw.trim() };
}

// ======================
// Lua script path: ends in .lua, max 256 chars
// ======================

export function parseLuaScriptPath(raw: unknown): SafeParseResult<string> {
  if (typeof raw !== 'string' || !raw.trim()) {
    return makeError('path', 'Script path is required');
  }
  const val = raw.trim();
  if (val.length > 256) {
    return makeError('path', 'Script path exceeds 256 characters');
  }
  if (!/^[a-zA-Z0-9_/-]+\.lua$/.test(val)) {
    return makeError('path', 'Path must be a valid Lua script path ending in .lua');
  }
  return { success: true, data: val };
}

// ======================
// Pulse type enum
// ======================

const PULSE_TYPES_ARR = ['perception', 'thought', 'action', 'system', 'ai_insight'];
const PULSE_TYPES = new Set(PULSE_TYPES_ARR);

export function parsePulseType(raw: unknown): SafeParseResult<string> {
  if (typeof raw !== 'string' || !raw.trim()) {
    return makeError('type', 'Pulse type is required');
  }
  if (!PULSE_TYPES.has(raw.trim())) {
    return makeError(
      'type',
      `Pulse type must be one of: ${PULSE_TYPES_ARR.join(', ')}`
    );
  }
  return { success: true, data: raw.trim() };
}

// ======================
// Full Sync Delta payload
// ======================

export interface SyncDelta {
  tenant_id: string;
  device_id?: string;
  nodes: SyncNode[];
  links: SyncLink[];
  pulses: SyncPulse[];
  last_sync_timestamp?: number;
}

export interface SyncNode {
  id?: string;
  name: string;
  type: string;
  subtype?: string;
  content?: Record<string, unknown>;
  pos_x?: number;
  pos_y?: number;
  pos_z?: number;
  color?: string;
  icon?: string;
  size?: number;
  importance?: number;
  deleted_at?: string;
}

export interface SyncLink {
  id?: string;
  source_id: string;
  target_id: string;
  type: string;
  weight?: number;
  content?: Record<string, unknown>;
  deleted_at?: string;
}

export interface SyncPulse {
  type: string;
  source_node_id?: string;
  target_node_id?: string;
  link_id?: string;
  text?: string;
  intensity?: number;
  metadata?: Record<string, unknown>;
}

export function parseSyncDelta(raw: unknown): SafeParseResult<SyncDelta> {
  if (!raw || typeof raw !== 'object') {
    return makeError('body', 'Request body must be a JSON object');
  }
  const obj = raw as Record<string, unknown>;

  const errors: ValidationError[] = [];

  // tenant_id (required)
  const tenantResult = parseTenantId(obj.tenant_id);
  if (!tenantResult.success) errors.push(...tenantResult.errors);

  // nodes (optional array, max 100)
  let nodes: SyncNode[] = [];
  if (obj.nodes !== undefined) {
    if (!Array.isArray(obj.nodes)) {
      errors.push(vError('nodes', 'nodes must be an array'));
    } else if (obj.nodes.length > 100) {
      errors.push(vError('nodes', 'Max 100 nodes per sync batch'));
    } else {
      nodes = obj.nodes as SyncNode[];
    }
  }

  // links (optional array, max 200)
  let links: SyncLink[] = [];
  if (obj.links !== undefined) {
    if (!Array.isArray(obj.links)) {
      errors.push(vError('links', 'links must be an array'));
    } else if (obj.links.length > 200) {
      errors.push(vError('links', 'Max 200 links per sync batch'));
    } else {
      links = obj.links as SyncLink[];
    }
  }

  // pulses (optional array, max 500)
  let pulses: SyncPulse[] = [];
  if (obj.pulses !== undefined) {
    if (!Array.isArray(obj.pulses)) {
      errors.push(vError('pulses', 'pulses must be an array'));
    } else if (obj.pulses.length > 500) {
      errors.push(vError('pulses', 'Max 500 pulses per sync batch'));
    } else {
      pulses = obj.pulses as SyncPulse[];
    }
  }

  // device_id (optional UUID)
  let device_id: string | undefined;
  if (obj.device_id !== undefined) {
    const parsed = parseTenantId(obj.device_id); // reuse UUID check
    if (parsed.success) {
      device_id = parsed.data;
    }
  }

  // last_sync_timestamp (optional positive integer)
  let last_sync_timestamp: number | undefined;
  if (obj.last_sync_timestamp !== undefined) {
    if (typeof obj.last_sync_timestamp !== 'number' || !Number.isInteger(obj.last_sync_timestamp) || obj.last_sync_timestamp <= 0) {
      errors.push(vError('last_sync_timestamp', 'Must be a positive integer'));
    } else {
      last_sync_timestamp = obj.last_sync_timestamp;
    }
  }

  if (errors.length > 0) {
    return { success: false, errors };
  }

  return {
    success: true,
    data: {
      tenant_id: (tenantResult as ParseResult<string>).data,
      device_id,
      nodes,
      links,
      pulses,
      last_sync_timestamp,
    },
  };
}

// ======================
// Wake word API — 3 mode: library | keyword | train
// ======================

const WAKEWORD_LIBRARY_IDS = new Set([
  'hi_esp',
  'hey_claw',
  'hi_lexin',
  'nihao',
]);

const WAKEWORD_MODES = new Set(['library', 'keyword', 'train']);

export type WakewordApplyMode = 'library' | 'keyword' | 'train';

export interface WakewordApplyBody {
  mode: WakewordApplyMode;
  deviceId: string;
  enabled: boolean;
  presetId?: string;
  keyword?: string;
  label?: string;
  samples?: WakewordTrainSample[];
}

function parseWakewordSamplesArray(
  raw: unknown,
  errors: ValidationError[]
): WakewordTrainSample[] {
  const samples: WakewordTrainSample[] = [];
  if (!Array.isArray(raw)) {
    errors.push(vError('samples', 'samples must be an array'));
    return samples;
  }
  if (raw.length > 60) {
    errors.push(vError('samples', 'Max 60 samples per request'));
    return samples;
  }
  for (let i = 0; i < raw.length; i++) {
    const s = raw[i] as Record<string, unknown>;
    const t = s?.type;
    if (t !== 'pos' && t !== 'neg') {
      errors.push(vError(`samples[${i}].type`, 'type must be pos or neg'));
      continue;
    }
    const isEspSource = s.source === 'esp';
    const b64 = s.audioBase64 ?? '';

    if (!isEspSource && (typeof b64 !== 'string' || b64.length < 100)) {
      errors.push(vError(`samples[${i}].audioBase64`, 'invalid audio data'));
      continue;
    }
    if (b64.length > 500_000) {
      errors.push(vError(`samples[${i}].audioBase64`, 'sample too large'));
      continue;
    }
    samples.push({
      type: t,
      audioBase64: b64 || undefined,
      durationMs: typeof s.durationMs === 'number' ? s.durationMs : undefined,
      source: s.source as string | undefined,
      fileName: typeof s.fileName === 'string' ? s.fileName : undefined,
    });
  }
  return samples;
}

export function parseWakewordApply(raw: unknown): SafeParseResult<WakewordApplyBody> {
  if (!raw || typeof raw !== 'object') {
    return makeError('body', 'Request body must be a JSON object');
  }
  const obj = raw as Record<string, unknown>;
  const errors: ValidationError[] = [];

  const devResult = parseDeviceId(obj.deviceId ?? obj.device_id);
  if (!devResult.success) errors.push(...devResult.errors);

  let mode: WakewordApplyMode | undefined;
  if (typeof obj.mode !== 'string' || !WAKEWORD_MODES.has(obj.mode)) {
    errors.push(vError('mode', 'mode must be library, keyword, or train'));
  } else {
    mode = obj.mode as WakewordApplyMode;
  }

  let enabled = true;
  if (obj.enabled !== undefined) {
    if (typeof obj.enabled === 'boolean') {
      enabled = obj.enabled;
    } else if (typeof obj.enabled === 'number') {
      enabled = obj.enabled !== 0;
    } else {
      errors.push(vError('enabled', 'enabled must be boolean or 0/1'));
    }
  }

  let presetId: string | undefined;
  let keyword: string | undefined;
  let label: string | undefined;
  let samples: WakewordTrainSample[] | undefined;

  if (mode === 'library') {
    const pid = obj.presetId ?? obj.preset;
    if (typeof pid !== 'string' || !WAKEWORD_LIBRARY_IDS.has(pid)) {
      errors.push(vError('presetId', 'Chọn preset từ thư viện có sẵn'));
    } else {
      presetId = pid;
    }
  }

  if (mode === 'keyword') {
    if (typeof obj.keyword !== 'string' || obj.keyword.trim().length < 2) {
      errors.push(vError('keyword', 'Từ khóa bắt buộc (2–32 ký tự)'));
    } else if (obj.keyword.trim().length > 32) {
      errors.push(vError('keyword', 'Từ khóa tối đa 32 ký tự'));
    } else {
      keyword = obj.keyword.trim();
    }
  }

  if (mode === 'train') {
    if (obj.label !== undefined) {
      if (typeof obj.label !== 'string' || obj.label.trim().length === 0 || obj.label.length > 32) {
        errors.push(vError('label', 'label max 32 chars'));
      } else {
        label = obj.label.trim();
      }
    } else {
      label = 'custom';
    }
    samples = parseWakewordSamplesArray(obj.samples, errors);
    const posCount = samples.filter((s) => s.type === 'pos').length;
    const negCount = samples.filter((s) => s.type === 'neg').length;
    if (posCount < 3) {
      errors.push(vError('samples', 'Cần ít nhất 3 mẫu dương'));
    }
    if (negCount < 3) {
      errors.push(vError('samples', 'Cần ít nhất 3 mẫu âm'));
    }
  }

  if (errors.length > 0 || !mode) {
    return { success: false, errors };
  }

  return {
    success: true,
    data: {
      mode,
      deviceId: (devResult as ParseResult<string>).data,
      enabled,
      presetId,
      keyword,
      label,
      samples,
    },
  };
}

export interface WakewordTrainSample {
  type: 'pos' | 'neg';
  /** Base64 audio. Empty for ESP-sourced samples (source: 'esp'). */
  audioBase64?: string;
  durationMs?: number;
  /** 'esp' = recorded from ESP mic; absent = from browser */
  source?: string;
  fileName?: string;
}

export interface WakewordTrainBody {
  deviceId: string;
  label: string;
  samples: WakewordTrainSample[];
}

export function parseWakewordTrain(raw: unknown): SafeParseResult<WakewordTrainBody> {
  if (!raw || typeof raw !== 'object') {
    return makeError('body', 'Request body must be a JSON object');
  }
  const obj = raw as Record<string, unknown>;
  const errors: ValidationError[] = [];

  const devResult = parseDeviceId(obj.deviceId ?? obj.device_id);
  if (!devResult.success) errors.push(...devResult.errors);

  let label = 'custom';
  if (obj.label !== undefined) {
    if (typeof obj.label !== 'string' || obj.label.trim().length === 0 || obj.label.length > 32) {
      errors.push(vError('label', 'label required, max 32 chars'));
    } else {
      label = obj.label.trim();
    }
  }

  const samples: WakewordTrainSample[] = [];
  if (!Array.isArray(obj.samples)) {
    errors.push(vError('samples', 'samples must be an array'));
  } else if (obj.samples.length > 60) {
    errors.push(vError('samples', 'Max 60 samples per request'));
  } else {
    for (let i = 0; i < obj.samples.length; i++) {
      const s = obj.samples[i] as Record<string, unknown>;
      const t = s?.type;
      if (t !== 'pos' && t !== 'neg') {
        errors.push(vError(`samples[${i}].type`, 'type must be pos or neg'));
        continue;
      }
      if (typeof s.audioBase64 !== 'string' || s.audioBase64.length < 100) {
        errors.push(vError(`samples[${i}].audioBase64`, 'invalid audio data'));
        continue;
      }
      if (s.audioBase64.length > 500_000) {
        errors.push(vError(`samples[${i}].audioBase64`, 'sample too large'));
        continue;
      }
      samples.push({
        type: t,
        audioBase64: s.audioBase64,
        durationMs:
          typeof s.durationMs === 'number' ? s.durationMs : undefined,
      });
    }
  }

  const posCount = samples.filter((s) => s.type === 'pos').length;
  const negCount = samples.filter((s) => s.type === 'neg').length;
  if (posCount < 3) {
    errors.push(vError('samples', 'Need at least 3 positive samples'));
  }
  if (negCount < 3) {
    errors.push(vError('samples', 'Need at least 3 negative samples'));
  }

  if (errors.length > 0) {
    return { success: false, errors };
  }

  return {
    success: true,
    data: {
      deviceId: (devResult as ParseResult<string>).data,
      label,
      samples,
    },
  };
}
