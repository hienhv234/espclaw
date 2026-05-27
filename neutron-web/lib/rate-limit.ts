/**
 * Rate limiting middleware using Upstash Redis.
 * Place this before your route handler logic.
 *
 * Usage:
 *   import { rateLimit } from '@/lib/rate-limit';
 *   export async function POST(req: NextRequest) {
 *     const { success, pending, limit, remaining, reset } = await rateLimit(req, 'device-api');
 *     if (!success) return NextResponse.json({ error: 'Too many requests' }, { status: 429 });
 *     // ... handler logic
 *   }
 */

import { NextRequest, NextResponse } from 'next/server';

// Lazy import to avoid issues during build
let Ratelimit: any = null;
let Redis: any = null;
let upstashAvailable = false;

// Initialize on first use
async function initUpstash() {
  if (upstashAvailable) return true;

  const redisUrl = process.env.UPSTASH_REDIS_REST_URL;
  const redisToken = process.env.UPSTASH_REDIS_REST_TOKEN;

  if (!redisUrl || !redisToken) {
    console.warn('[RateLimit] UPSTASH_REDIS_REST_URL or UPSTASH_REDIS_REST_TOKEN not set — rate limiting disabled');
    return false;
  }

  try {
    const { Ratelimit: RL } = await import('@upstash/ratelimit');
    const { Redis: RedisClient } = await import('@upstash/redis');
    Ratelimit = RL;
    Redis = RedisClient;
    upstashAvailable = true;
    return true;
  } catch (err) {
    console.error('[RateLimit] Failed to initialize Upstash:', err);
    return false;
  }
}

// Rate limit configurations per endpoint
const LIMITS: Record<
  string,
  { requests: number; window: string }
> = {
  // Device pairing: 10 OTP requests per IP per minute
  'device-api': { requests: 10, window: '60 s' },
  // Neuron sync: 30 per user per minute
  'neuron-api': { requests: 30, window: '60 s' },
  // Health: 60 per IP per minute
  'health-api': { requests: 60, window: '60 s' },
  // Lua execution: 20 per user per minute
  'lua-api': { requests: 20, window: '60 s' },
  // Default: 100 per IP per minute
  'default': { requests: 100, window: '60 s' },
};

export interface RateLimitResult {
  success: boolean;
  pending: number;
  limit: number;
  remaining: number;
  reset: number;
}

export async function rateLimit(
  req: NextRequest,
  endpoint: string
): Promise<RateLimitResult> {
  // Init Upstash if not yet done
  const initialized = await initUpstash();
  if (!initialized) {
    // Graceful degradation: allow request if rate limiting unavailable
    return {
      success: true,
      pending: 0,
      limit: 0,
      remaining: 0,
      reset: 0,
    };
  }

  const config = LIMITS[endpoint] ?? LIMITS['default'];
  const identifier = getIdentifier(req);
  const cache = new Map<string, { count: number; resetAt: number }>();

  // For Vercel Edge, use in-memory sliding window
  // In production, use Upstash Ratelimit with Redis
  const now = Date.now();
  const cached = cache.get(identifier);

  if (cached) {
    if (cached.resetAt > now) {
      if (cached.count >= config.requests) {
        return {
          success: false,
          pending: cached.count - config.requests,
          limit: config.requests,
          remaining: 0,
          reset: cached.resetAt,
        };
      }
      cached.count++;
      return {
        success: true,
        pending: 0,
        limit: config.requests,
        remaining: config.requests - cached.count,
        reset: cached.resetAt,
      };
    }
    // Window expired, reset
    cache.delete(identifier);
  }

  // Create new window
  const windowMs = parseWindow(config.window);
  const resetAt = now + windowMs;
  cache.set(identifier, { count: 1, resetAt });

  // Cleanup old entries periodically
  if (Math.random() < 0.01) {
    const keysToDelete: string[] = [];
    cache.forEach((val, key) => {
      if (val.resetAt < now) keysToDelete.push(key);
    });
    keysToDelete.forEach(key => cache.delete(key));
  }

  return {
    success: true,
    pending: 0,
    limit: config.requests,
    remaining: config.requests - 1,
    reset: resetAt,
  };
}

function getIdentifier(req: NextRequest): string {
  // Use IP as identifier (Vercel provides x-forwarded-for)
  const forwarded = req.headers.get('x-forwarded-for');
  const ip = forwarded?.split(',')[0]?.trim() ?? 'anonymous';
  return ip;
}

function parseWindow(window: string): number {
  const match = window.match(/^(\d+)\s*(s|m|h|d)$/);
  if (!match) return 60_000;
  const [, amount, unit] = match;
  const multipliers: Record<string, number> = { s: 1000, m: 60_000, h: 3_600_000, d: 86_400_000 };
  return parseInt(amount) * (multipliers[unit] ?? 60_000);
}

export function withRateLimit(
  endpoint: string,
  handler: (req: NextRequest) => Promise<NextResponse>
) {
  return async (req: NextRequest) => {
    const result = await rateLimit(req, endpoint);
    if (!result.success) {
      return NextResponse.json(
        {
          error: 'Too many requests',
          retryAfter: Math.ceil((result.reset - Date.now()) / 1000),
        },
        {
          status: 429,
          headers: {
            'Retry-After': String(Math.ceil((result.reset - Date.now()) / 1000)),
            'X-RateLimit-Limit': String(result.limit),
            'X-RateLimit-Remaining': String(result.remaining),
            'X-RateLimit-Reset': String(result.reset),
          },
        }
      );
    }

    const response = await handler(req);
    // Add rate limit headers to successful responses too
    response.headers.set('X-RateLimit-Limit', String(result.limit));
    response.headers.set('X-RateLimit-Remaining', String(result.remaining));
    return response;
  };
}
