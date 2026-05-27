/**
 * Structured logging without pino (no external deps needed).
 * JSON output in production, pretty in development.
 */

export interface LogContext {
  requestId?: string;
  tenantId?: string;
  deviceId?: string;
  userId?: string;
  route?: string;
  method?: string;
  duration_ms?: number;
  statusCode?: number;
  [key: string]: unknown;
}

type LogLevel = 'debug' | 'info' | 'warn' | 'error';

const LEVEL_PRIORITY: Record<LogLevel, number> = {
  debug: 0,
  info: 1,
  warn: 2,
  error: 3,
};

const currentLevel: LogLevel =
  (process.env.LOG_LEVEL as LogLevel) ??
  (process.env.NODE_ENV === 'production' ? 'info' : 'debug');

const isDev = process.env.NODE_ENV !== 'production';

function formatMessage(level: LogLevel, msg: string, context?: LogContext): object {
  return {
    level,
    msg,
    time: new Date().toISOString(),
    service: 'neutron-web',
    env: isDev ? 'development' : 'production',
    ...context,
  };
}

function shouldLog(level: LogLevel): boolean {
  return LEVEL_PRIORITY[level] >= LEVEL_PRIORITY[currentLevel];
}

function output(log: object) {
  if (isDev) {
    // Pretty print in development
    const lvl = (log as any).level as string;
    const color =
      lvl === 'error' ? '\x1b[31m' :
      lvl === 'warn'  ? '\x1b[33m' :
      lvl === 'info'  ? '\x1b[36m' : '\x1b[90m';
    const reset = '\x1b[0m';
    const msg = (log as any).msg ?? '';
    const extra = Object.entries(log as object)
      .filter(([k]) => k !== 'level' && k !== 'msg' && k !== 'time' && k !== 'service' && k !== 'env')
      .map(([k, v]) => ` ${k}=${JSON.stringify(v)}`)
      .join('');
    console.log(`${color}[${lvl.toUpperCase()}]${reset} ${msg}${extra}`);
  } else {
    // JSON in production
    console.log(JSON.stringify(log));
  }
}

function log(level: LogLevel, msg: string, context?: LogContext) {
  if (!shouldLog(level)) return;
  output(formatMessage(level, msg, context));
}

export const logger = {
  debug(msg: string, ctx?: LogContext) { log('debug', msg, ctx); },
  info(msg: string, ctx?: LogContext)  { log('info',  msg, ctx); },
  warn(msg: string, ctx?: LogContext)  { log('warn',  msg, ctx); },
  error(msg: string, ctx?: LogContext) { log('error', msg, ctx); },
  child(_ctx: LogContext) {
    // Return same logger — context included in each call
    return logger;
  },
};

export function withContext(context: LogContext) {
  return logger;
}

export function generateRequestId(): string {
  return `${Date.now().toString(36)}-${Math.random().toString(36).substring(2, 8)}`;
}

export const mqttLogger   = { debug: (m: string, c?: LogContext) => logger.debug(`[mqtt] ${m}`, c), info: (m: string, c?: LogContext) => logger.info(`[mqtt] ${m}`, c), warn: (m: string, c?: LogContext) => logger.warn(`[mqtt] ${m}`, c), error: (m: string, c?: LogContext) => logger.error(`[mqtt] ${m}`, c), child: (_: LogContext) => mqttLogger };
export const dbLogger    = { debug: (m: string, c?: LogContext) => logger.debug(`[db] ${m}`, c), info: (m: string, c?: LogContext) => logger.info(`[db] ${m}`, c), warn: (m: string, c?: LogContext) => logger.warn(`[db] ${m}`, c), error: (m: string, c?: LogContext) => logger.error(`[db] ${m}`, c), child: (_: LogContext) => dbLogger };
export const authLogger   = { debug: (m: string, c?: LogContext) => logger.debug(`[auth] ${m}`, c), info: (m: string, c?: LogContext) => logger.info(`[auth] ${m}`, c), warn: (m: string, c?: LogContext) => logger.warn(`[auth] ${m}`, c), error: (m: string, c?: LogContext) => logger.error(`[auth] ${m}`, c), child: (_: LogContext) => authLogger };
export const syncLogger   = { debug: (m: string, c?: LogContext) => logger.debug(`[sync] ${m}`, c), info: (m: string, c?: LogContext) => logger.info(`[sync] ${m}`, c), warn: (m: string, c?: LogContext) => logger.warn(`[sync] ${m}`, c), error: (m: string, c?: LogContext) => logger.error(`[sync] ${m}`, c), child: (_: LogContext) => syncLogger };
export const apiLogger    = { debug: (m: string, c?: LogContext) => logger.debug(`[api] ${m}`, c), info: (m: string, c?: LogContext) => logger.info(`[api] ${m}`, c), warn: (m: string, c?: LogContext) => logger.warn(`[api] ${m}`, c), error: (m: string, c?: LogContext) => logger.error(`[api] ${m}`, c), child: (_: LogContext) => apiLogger };
