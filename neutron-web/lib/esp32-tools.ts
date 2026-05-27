/**
 * ESP32 Tool Registry
 * Maps all 20 built-in ESP32 tools with their parameters.
 * Used by workflow editor UI and execution engine.
 * Source: main/tool/builtin_tools.def
 */

export interface ToolParam {
  name: string;
  type: 'string' | 'integer' | 'number' | 'boolean';
  description: string;
  required?: boolean;
  enum?: string[];
  min?: number;
  max?: number;
}

export interface ToolDefinition {
  name: string;
  description: string;
  category: 'gpio' | 'memory' | 'system' | 'cron' | 'time' | 'persona' | 'network';
  parameters: ToolParam[];
  example?: Record<string, unknown>;
}

export const ESP32_TOOLS: ToolDefinition[] = [
  // ── GPIO ───────────────────────────────────────────────────────────
  {
    name: 'gpio_write',
    description: 'Set GPIO pin HIGH (1) or LOW (0)',
    category: 'gpio',
    parameters: [
      { name: 'pin', type: 'integer', description: 'GPIO pin number', required: true, min: 0, max: 48 },
      { name: 'state', type: 'integer', description: '0 = LOW (off), 1 = HIGH (on)', required: true, enum: ['0', '1'] },
    ],
    example: { pin: 2, state: 1 },
  },
  {
    name: 'gpio_read',
    description: 'Read current state of a GPIO pin',
    category: 'gpio',
    parameters: [
      { name: 'pin', type: 'integer', description: 'GPIO pin number', required: true, min: 0, max: 48 },
    ],
    example: { pin: 4 },
  },
  {
    name: 'gpio_read_all',
    description: 'Read states of all configured GPIO pins',
    category: 'gpio',
    parameters: [],
    example: {},
  },

  // ── Memory (NVS) ──────────────────────────────────────────────────
  {
    name: 'memory_set',
    description: 'Store a key-value pair in NVS. Key must start with "u_"',
    category: 'memory',
    parameters: [
      { name: 'key', type: 'string', description: 'Storage key (must start with u_*)', required: true },
      { name: 'value', type: 'string', description: 'Value to store', required: true },
    ],
    example: { key: 'u_brightness', value: '80' },
  },
  {
    name: 'memory_get',
    description: 'Read a value from NVS by key',
    category: 'memory',
    parameters: [
      { name: 'key', type: 'string', description: 'Storage key to read', required: true },
    ],
    example: { key: 'u_brightness' },
  },
  {
    name: 'memory_delete',
    description: 'Delete a key from NVS',
    category: 'memory',
    parameters: [
      { name: 'key', type: 'string', description: 'Storage key to delete', required: true },
    ],
    example: { key: 'u_temp_session' },
  },
  {
    name: 'memory_list',
    description: 'List all stored user memory keys',
    category: 'memory',
    parameters: [],
    example: {},
  },

  // ── System ────────────────────────────────────────────────────────
  {
    name: 'get_diagnostics',
    description: 'Get ESP32 diagnostics: free heap, uptime, chip info',
    category: 'system',
    parameters: [],
    example: {},
  },
  {
    name: 'get_version',
    description: 'Get firmware version and build info',
    category: 'system',
    parameters: [],
    example: {},
  },

  // ── Cron ──────────────────────────────────────────────────────────
  {
    name: 'cron_schedule',
    description: 'Schedule a recurring task',
    category: 'cron',
    parameters: [
      {
        name: 'type', type: 'string', description: 'Schedule type', required: true,
        enum: ['periodic', 'daily', 'once'],
      },
      { name: 'action', type: 'string', description: 'Natural language action to perform', required: true },
      { name: 'interval_seconds', type: 'integer', description: 'For periodic: interval in seconds', min: 10, max: 86400 },
      { name: 'hour', type: 'integer', description: 'For daily: hour (0-23)', min: 0, max: 23 },
      { name: 'minute', type: 'integer', description: 'For daily: minute (0-59)', min: 0, max: 59 },
      { name: 'delay_seconds', type: 'integer', description: 'Delay before executing (seconds)', min: 0, max: 3600 },
    ],
    example: { type: 'daily', action: 'Check temperature sensor', hour: 8, minute: 0 },
  },
  {
    name: 'cron_list',
    description: 'List all currently scheduled tasks',
    category: 'cron',
    parameters: [],
    example: {},
  },
  {
    name: 'cron_cancel',
    description: 'Cancel a scheduled task by ID',
    category: 'cron',
    parameters: [
      { name: 'id', type: 'integer', description: 'Task ID to cancel', required: true },
    ],
    example: { id: 3 },
  },
  {
    name: 'cron_cancel_all',
    description: 'Cancel all scheduled tasks',
    category: 'cron',
    parameters: [],
    example: {},
  },

  // ── Time ──────────────────────────────────────────────────────────
  {
    name: 'get_time',
    description: 'Get current time and timezone',
    category: 'time',
    parameters: [],
    example: {},
  },
  {
    name: 'set_timezone',
    description: 'Set timezone (e.g. Asia/Ho_Chi_Minh, UTC)',
    category: 'time',
    parameters: [
      { name: 'timezone', type: 'string', description: 'Timezone string', required: true },
    ],
    example: { timezone: 'Asia/Ho_Chi_Minh' },
  },

  // ── Persona ───────────────────────────────────────────────────────
  {
    name: 'set_persona',
    description: 'Change AI assistant personality',
    category: 'persona',
    parameters: [
      {
        name: 'persona', type: 'string', description: 'Personality style', required: true,
        enum: ['neutral', 'friendly', 'technical', 'witty'],
      },
    ],
    example: { persona: 'friendly' },
  },
  {
    name: 'get_persona',
    description: 'Get current AI assistant personality',
    category: 'persona',
    parameters: [],
    example: {},
  },

  // ── Network ───────────────────────────────────────────────────────
  {
    name: 'wifi_scan',
    description: 'Scan for nearby WiFi networks (brief disconnect)',
    category: 'network',
    parameters: [],
    example: {},
  },
  {
    name: 'get_network_info',
    description: 'Get IP, gateway, DNS, RSSI, MAC address, SSID',
    category: 'network',
    parameters: [],
    example: {},
  },
];

// ── Delay (special - not in ESP32 but useful for workflows) ────────────────
export const DELAY_TOOL: ToolDefinition = {
  name: 'delay',
  description: 'Wait/delay for N milliseconds (workflow step only, not an ESP32 tool)',
  category: 'system',
  parameters: [
    { name: 'milliseconds', type: 'integer', description: 'Delay in ms', required: true, min: 100, max: 60000 },
  ],
  example: { milliseconds: 500 },
};

// ── Categories ────────────────────────────────────────────────────────────
export const TOOL_CATEGORIES = {
  gpio: { label: 'GPIO', icon: '💡', color: '#fbbf24' },
  memory: { label: 'Memory', icon: '🧠', color: '#a78bfa' },
  system: { label: 'System', icon: '⚙️', color: '#60a5fa' },
  cron: { label: 'Cron', icon: '⏰', color: '#34d399' },
  time: { label: 'Time', icon: '🕐', color: '#f472b6' },
  persona: { label: 'Persona', icon: '🎭', color: '#fb923c' },
  network: { label: 'Network', icon: '📡', color: '#22d3ee' },
} as const;

// ── Quick templates for common workflows ─────────────────────────────────
export const WORKFLOW_TEMPLATES = [
  {
    id: 'gpio_blink',
    name: 'LED Blink',
    icon: '💡',
    description: 'Blink an LED on GPIO pin',
    category: 'automation',
    trigger_pattern: 'blink|bật đèn|turn on',
    steps: [
      { id: 0, type: 'tool' as const, tool: 'gpio_write', params: { pin: 2, state: 1 }, description: 'Turn LED on' },
      { id: 1, type: 'wait' as const, milliseconds: 500, description: 'Wait 500ms' },
      { id: 2, type: 'tool' as const, tool: 'gpio_write', params: { pin: 2, state: 0 }, description: 'Turn LED off' },
    ],
  },
  {
    id: 'temperature_check',
    name: 'Temperature Check',
    icon: '🌡️',
    description: 'Store temperature reading to memory',
    category: 'monitoring',
    trigger_pattern: 'nhiệt độ|temperature|check temp',
    steps: [
      { id: 0, type: 'tool' as const, tool: 'memory_set', params: { key: 'u_last_temp_check', value: '{{NOW}}' }, description: 'Record timestamp' },
      { id: 1, type: 'tool' as const, tool: 'get_diagnostics', params: {}, description: 'Get system info' },
    ],
  },
  {
    id: 'morning_routine',
    name: 'Morning Routine',
    icon: '☀️',
    description: 'Good morning automation',
    category: 'automation',
    trigger_pattern: 'chào buổi sáng|good morning',
    steps: [
      { id: 0, type: 'tool' as const, tool: 'get_time', params: {}, description: 'Get current time' },
      { id: 1, type: 'tool' as const, tool: 'get_diagnostics', params: {}, description: 'System check' },
      { id: 2, type: 'tool' as const, tool: 'set_persona', params: { persona: 'friendly' }, description: 'Set friendly persona' },
    ],
  },
  {
    id: 'wifi_status',
    name: 'WiFi Status Report',
    icon: '📡',
    description: 'Get WiFi status and network info',
    category: 'monitoring',
    trigger_pattern: 'wifi|internet|mạng',
    steps: [
      { id: 0, type: 'tool' as const, tool: 'get_network_info', params: {}, description: 'Get network info' },
      { id: 1, type: 'tool' as const, tool: 'wifi_scan', params: {}, description: 'Scan networks' },
    ],
  },
  {
    id: 'memory_stats',
    name: 'Memory Stats',
    icon: '📊',
    description: 'Show memory usage and stored data',
    category: 'monitoring',
    trigger_pattern: 'bộ nhớ|memory|dữ liệu',
    steps: [
      { id: 0, type: 'tool' as const, tool: 'memory_list', params: {}, description: 'List memory keys' },
      { id: 1, type: 'tool' as const, tool: 'get_diagnostics', params: {}, description: 'Get system diagnostics' },
    ],
  },
];

// Helper: get tool by name
export function getTool(name: string): ToolDefinition | undefined {
  return ESP32_TOOLS.find(t => t.name === name) ?? (name === 'delay' ? DELAY_TOOL : undefined);
}

// Helper: get tools by category
export function getToolsByCategory(category: string): ToolDefinition[] {
  return ESP32_TOOLS.filter(t => t.category === category);
}
