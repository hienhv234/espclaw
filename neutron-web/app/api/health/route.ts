/**
 * Health Check Endpoint
 * GET /api/health
 *
 * Returns system health status for load balancers and monitoring.
 * This endpoint is intentionally NOT rate-limited (it needs to be reachable
 * even under heavy load to report failures).
 */
import { NextRequest, NextResponse } from 'next/server';

interface HealthStatus {
  status: 'healthy' | 'degraded' | 'unhealthy';
  version: string;
  timestamp: string;
  uptime_seconds: number;
  checks: {
    supabase: boolean;
    mqtt: boolean;
    environment: boolean;
  };
  limits?: {
    rateLimitEnabled: boolean;
    redisAvailable: boolean;
  };
}

export async function GET(request: NextRequest): Promise<NextResponse> {
  const checks: HealthStatus['checks'] = {
    supabase: false,
    mqtt: false,
    environment: false,
  };

  const start = Date.now();

  // 1. Check environment variables
  try {
    const required = [
      'NEXT_PUBLIC_SUPABASE_URL',
      'SUPABASE_SERVICE_ROLE_KEY',
      'NEXT_PUBLIC_MQTT_BROKER_URL',
      'MQTT_USERNAME',
      'MQTT_PASSWORD',
    ];
    const missing = required.filter(
      (key) => !process.env[key]
    );
    checks.environment = missing.length === 0;
  } catch {
    checks.environment = false;
  }

  // 2. Check Supabase connectivity
  try {
    const supabaseUrl = process.env.NEXT_PUBLIC_SUPABASE_URL;
    const supabaseKey = process.env.SUPABASE_SERVICE_ROLE_KEY;
    if (supabaseUrl && supabaseKey) {
      const res = await fetch(`${supabaseUrl}/rest/v1/tenants?select=id&limit=1`, {
        headers: {
          apikey: supabaseKey,
          Authorization: `Bearer ${supabaseKey}`,
        },
        next: { revalidate: 0 },
      });
      checks.supabase = res.ok;
    }
  } catch {
    checks.supabase = false;
  }

  // 3. Check MQTT broker reachability (TCP check)
  try {
    const brokerUrl = process.env.NEXT_PUBLIC_MQTT_BROKER_URL;
    if (brokerUrl) {
      const url = new URL(brokerUrl);
      // Use fetch to check if broker host is reachable
      const reachable = await fetch(`https://${url.host}`, {
        method: 'HEAD',
        signal: AbortSignal.timeout(3000),
      }).then(() => true).catch(() => false);
      checks.mqtt = reachable;
    }
  } catch {
    checks.mqtt = false;
  }

  const isHealthy = checks.supabase && checks.environment;
  const isDegraded = !checks.supabase && checks.environment;

  const health: HealthStatus = {
    status: isHealthy ? 'healthy' : isDegraded ? 'degraded' : 'unhealthy',
    version: '1.0.0',
    timestamp: new Date().toISOString(),
    uptime_seconds: Math.floor(process.uptime()),
    checks,
  };

  const statusCode = isHealthy ? 200 : isDegraded ? 200 : 503;

  return NextResponse.json(health, { status: statusCode });
}
