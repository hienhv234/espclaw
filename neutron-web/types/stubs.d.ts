// Stub type declarations for packages that might not be installed
// These allow TypeScript to compile without requiring npm install

declare module 'zod' {
  interface ZodTypeDef {
    errorMap?: any;
  }
  export const z: any;
  export type ZodSchema<T = any> = any;
}

declare module 'pino' {
  export default function pino(options?: any): any;
  export const stdTimeFunctions: any;
}

declare module 'pino-pretty' {
  const pinoPretty: any;
  export default pinoPretty;
}

declare module '@upstash/ratelimit' {
  export class Ratelimit {
    constructor(config: any);
    limit(identifier: string): Promise<any>;
  }
}

declare module '@upstash/redis' {
  export class Redis {
    constructor(config: any);
    get(key: string): Promise<any>;
    set(key: string, value: any, options?: any): Promise<any>;
    incr(key: string): Promise<number>;
    expire(key: string, seconds: number): Promise<any>;
  }
}

declare module 'mqtt' {
  export interface MqttClient {
    on(event: string, callback: (...args: any[]) => void): this;
    subscribe(topic: string, options: any, callback?: (err?: Error) => void): this;
    publish(topic: string, message: string, options: any, callback?: (err?: Error) => void): this;
    end(force?: boolean): void;
    connected: boolean;
  }
  export interface IClientOptions {
    username?: string;
    password?: string;
    clientId?: string;
    rejectUnauthorized?: boolean;
    connectTimeout?: number;
    keepalive?: number;
    reconnectPeriod?: number;
    clean?: boolean;
  }
  export function connect(url: string, options?: IClientOptions): MqttClient;
}
