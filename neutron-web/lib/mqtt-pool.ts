import mqtt, { MqttClient, IClientOptions } from 'mqtt';
import { createClient } from '@supabase/supabase-js';

/**
 * Singleton MQTT connection pool with automatic reconnection.
 * Replaces connect-per-request pattern with persistent connection.
 */

const supabaseUrl = process.env.NEXT_PUBLIC_SUPABASE_URL!;
const supabaseKey = process.env.SUPABASE_SERVICE_ROLE_KEY!;

class MQTTPool {
  private static instance: MQTTPool;
  private client: MqttClient | null = null;
  private connectPromise: Promise<MqttClient> | null = null;
  private reconnectAttempts = 0;
  private maxReconnectAttempts = 10;
  private reconnectDelayMs = 1000;
  private isDestroyed = false;

  private constructor() {}

  public static getInstance(): MQTTPool {
    if (!MQTTPool.instance) {
      MQTTPool.instance = new MQTTPool();
    }
    return MQTTPool.instance;
  }

  private getBrokerUrl(): string {
    const url = process.env.NEXT_PUBLIC_MQTT_BROKER_URL;
    if (!url) {
      throw new Error('NEXT_PUBLIC_MQTT_BROKER_URL is not set');
    }
    return url;
  }

  private getUsername(): string {
    const username = process.env.MQTT_USERNAME;
    if (!username) {
      throw new Error('MQTT_USERNAME is not set');
    }
    return username;
  }

  private getPassword(): string {
    const password = process.env.MQTT_PASSWORD;
    if (!password) {
      throw new Error('MQTT_PASSWORD is not set');
    }
    return password;
  }

  private getClientId(): string {
    return `backend-${process.pid}-${Math.random().toString(16).substring(2, 10)}`;
  }

  private getMqttOptions(): IClientOptions {
    return {
      username: this.getUsername(),
      password: this.getPassword(),
      clientId: this.getClientId(),
      rejectUnauthorized: true, // ⚠️ FIXED: Enable TLS certificate validation
      connectTimeout: 15000,
      keepalive: 60,
      reconnectPeriod: 0, // We handle reconnection manually
      clean: true,
    };
  }

  private async connect(): Promise<MqttClient> {
    if (this.isDestroyed) {
      throw new Error('MQTTPool has been destroyed');
    }

    // Return existing client if connected
    if (this.client?.connected) {
      return this.client;
    }

    // Return pending connect if already connecting
    if (this.connectPromise) {
      return this.connectPromise;
    }

    this.connectPromise = new Promise((resolve, reject) => {
      const url = this.getBrokerUrl();
      const options = this.getMqttOptions();

      console.log(`[MQTTPool] Connecting to ${url}...`);

      const client = mqtt.connect(url, options);
      this.client = client;

      const onConnect = () => {
        console.log('[MQTTPool] Connected successfully');
        this.reconnectAttempts = 0;
        this.reconnectDelayMs = 1000;
        this.subscribeHeartbeats();
        resolve(client);
        this.cleanup();
      };

      const onError = (err: Error) => {
        console.error('[MQTTPool] Connection error:', err.message);
        reject(err);
        this.cleanup();
      };

      const onClose = () => {
        if (!this.isDestroyed) {
          console.warn('[MQTT] Connection closed unexpectedly, attempting reconnect...');
          this.scheduleReconnect(resolve, reject);
        }
      };

      client.on('connect', onConnect);
      client.on('error', onError);
      client.on('close', onClose);

      // Connection timeout
      setTimeout(() => {
        if (!client.connected) {
          client.end();
          reject(new Error('MQTT connection timeout after 15s'));
          this.cleanup();
        }
      }, 15000);
    });

    return this.connectPromise;
  }

  private cleanup() {
    this.connectPromise = null;
  }

  private scheduleReconnect(
    originalResolve: (client: MqttClient) => void,
    originalReject: (err: Error) => void
  ) {
    if (this.isDestroyed) return;

    if (this.reconnectAttempts >= this.maxReconnectAttempts) {
      originalReject(new Error(`MQTT reconnection failed after ${this.maxReconnectAttempts} attempts`));
      return;
    }

    this.reconnectAttempts++;
    const delay = Math.min(this.reconnectDelayMs * this.reconnectAttempts, 30000);

    console.log(`[MQTTPool] Reconnecting in ${delay}ms (attempt ${this.reconnectAttempts}/${this.maxReconnectAttempts})`);

    setTimeout(async () => {
      try {
        const client = await this.connect();
        originalResolve(client);
      } catch (err) {
        originalReject(err as Error);
      }
    }, delay);
  }

  /**
   * Publish a message to a topic with QoS 1 (at least once delivery).
   */
  public async publish(
    topic: string,
    payload: string | object,
    qos: 0 | 1 | 2 = 1
  ): Promise<void> {
    const client = await this.connect();

    return new Promise((resolve, reject) => {
      const message = typeof payload === 'string' ? payload : JSON.stringify(payload);

      client.publish(topic, message, { qos }, (error) => {
        if (error) {
          console.error(`[MQTTPool] Publish error on ${topic}:`, error.message);
          reject(error);
        } else {
          console.log(`[MQTTPool] Published to ${topic}`);
          resolve();
        }
      });
    });
  }

  /**
   * Subscribe to a topic (exact match, text payload).
   */
  public async subscribe(
    topic: string,
    callback: (topic: string, message: string) => void,
    qos: 0 | 1 | 2 = 1
  ): Promise<void> {
    const client = await this.ensureConnected();

    return new Promise((resolve, reject) => {
      client.subscribe(topic, { qos }, (error) => {
        if (error) {
          reject(error);
        } else {
          client.on('message', (receivedTopic, message) => {
            if (receivedTopic === topic) {
              callback(receivedTopic, message.toString());
            }
          });
          resolve();
        }
      });
    });
  }

  /**
   * Subscribe with wildcard support (+ single-level, # multi-level).
   * handler(topic, payload) — payload is raw Buffer (binary-safe).
   */
  public subscribeRaw(
    topic: string,
    handler: (topic: string, payload: Buffer) => void,
    qos: 0 | 1 | 2 = 1
  ): void {
    if (!this.client?.connected) {
      console.warn('[MQTTPool] subscribeRaw called before connected — queuing...');
      this.connect().then(() => this.subscribeRaw(topic, handler, qos));
      return;
    }
    this.client.subscribe(topic, { qos }, (err) => {
      if (err) {
        console.error(`[MQTTPool] subscribeRaw failed for ${topic}:`, err.message);
        return;
      }
      console.log(`[MQTTPool] Subscribed raw to ${topic}`);
    });
    this.client.on('message', (receivedTopic, message) => {
      if (this._topicMatches(receivedTopic, topic)) {
        handler(receivedTopic, Buffer.from(message));
      }
    });
  }

  private _topicMatches(received: string, pattern: string): boolean {
    const rp = received.split('/');
    const pp = pattern.split('/');
    for (let i = 0; i < pp.length; i++) {
      if (pp[i] === '#') return true;
      if (i >= rp.length) return false;
      if (pp[i] !== '+' && pp[i] !== rp[i]) return false;
    }
    return pp.length === rp.length;
  }

  /**
   * Check if connected.
   */
  public isConnected(): boolean {
    return this.client?.connected ?? false;
  }

  /**
   * Ensure the pool is connected. Call on server startup.
   */
  public async ensureConnected(): Promise<MqttClient> {
    return this.connect();
  }

  /**
   * Destroy the pool (for graceful shutdown).
   */
  public destroy(): void {
    this.isDestroyed = true;
    if (this.client) {
      this.client.end();
      this.client = null;
    }
    this.connectPromise = null;
    console.log('[MQTTPool] Destroyed');
  }

  private async subscribeHeartbeats(): Promise<void> {
    const client = this.client;
    if (!client) return;

    client.subscribe('espclaw/+/status', { qos: 1 }, (err) => {
      if (err) {
        console.error('[MQTTPool] Failed to subscribe to heartbeat topic:', err.message);
        return;
      }
      console.log('[MQTTPool] Subscribed to espclaw/+/status');
    });

    client.on('message', async (topic, message) => {
      // Expected topic: espclaw/{deviceId}/status
      const parts = topic.split('/');
      if (parts.length !== 3 || parts[0] !== 'espclaw' || parts[2] !== 'status') return;

      const deviceId = parts[1];
      const status = message.toString().trim().toLowerCase();
      const isOnline = status === 'online' || status === '1' || status === 'true';

      try {
        const supabase = createClient(supabaseUrl, supabaseKey);
        const { error } = await supabase
          .from('devices')
          .update({
            is_online: isOnline,
            last_seen_at: new Date().toISOString(),
          })
          .eq('mac_address', deviceId);

        if (error) {
          console.warn(`[MQTTPool] Failed to update device ${deviceId} online status:`, error.message);
        } else {
          console.log(`[MQTTPool] Device ${deviceId} set ${isOnline ? 'online' : 'offline'}`);
        }
      } catch (e) {
        console.warn(`[MQTTPool] Heartbeat DB update error for ${deviceId}:`, e);
      }
    });
  }
}

// Export singleton getter
export function getMQTTPool(): MQTTPool {
  return MQTTPool.getInstance();
}

// Backwards-compatible exports using the pool
export async function publishOTP(deviceId: string, otp: string): Promise<void> {
  const pool = getMQTTPool();
  await pool.ensureConnected();
  const topic = `espclaw/${deviceId}/otp`;
  const payload = JSON.stringify({ otp });
  return pool.publish(topic, payload, 1);
}

export async function publishLoginSuccess(
  deviceId: string, 
  tenantId: string
): Promise<void> {
  const pool = getMQTTPool();
  await pool.ensureConnected();
  const topic = `espclaw/${deviceId}/login_success`;
  const payload = JSON.stringify({ 
    status: 'success', 
    tenant_id: tenantId
  });
  return pool.publish(topic, payload, 1);
}

export async function publishMessage(
  deviceId: string,
  message: string,
  qos: 0 | 1 | 2 = 1
): Promise<void> {
  const pool = getMQTTPool();
  await pool.ensureConnected();
  const topic = `espclaw/${deviceId}/cmd`;
  const payload = JSON.stringify({ message });
  return pool.publish(topic, payload, qos);
}

export async function publishNeuronPulse(
  deviceId: string,
  pulse: object,
  qos: 0 | 1 | 2 = 1
): Promise<void> {
  const pool = getMQTTPool();
  await pool.ensureConnected();
  const topic = `espclaw/${deviceId}/pulse`;
  return pool.publish(topic, pulse, qos);
}
