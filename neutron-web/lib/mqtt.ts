/**
 * @deprecated Use `mqtt-pool.ts` instead.
 * This file is kept for backwards compatibility only.
 *
 * The new mqtt-pool.ts provides:
 * - Singleton connection pool (no connect-per-request)
 * - Automatic reconnection with exponential backoff
 * - rejectUnauthorized: true (TLS verified)
 * - Connection state management
 */
export {
  getMQTTPool,
  publishOTP,
  publishLoginSuccess,
  publishMessage,
  publishNeuronPulse,
} from './mqtt-pool';
