/**
 * Supabase Auth Helpers
 * Browser and server auth utilities
 */

import { createClient, SupabaseClient, User } from '@supabase/supabase-js';

const supabaseUrl = process.env.NEXT_PUBLIC_SUPABASE_URL!;
const supabaseAnonKey = process.env.NEXT_PUBLIC_SUPABASE_ANON_KEY!;

// Browser client
let browserClient: SupabaseClient | null = null;

export const getSupabaseClient = () => {
  if (!browserClient) {
    browserClient = createClient(supabaseUrl, supabaseAnonKey);
  }
  return browserClient;
};

// Auth state
export interface AuthState {
  user: User | null;
  loading: boolean;
}

// Sign up with email
export async function signUp(email: string, password: string, name?: string) {
  const client = getSupabaseClient();
  
  const { data, error } = await client.auth.signUp({
    email,
    password,
    options: {
      data: {
        name: name || email.split('@')[0],
      },
    },
  });
  
  if (error) throw error;
  return data;
}

// Sign in with email/password
export async function signIn(email: string, password: string) {
  const client = getSupabaseClient();
  
  const { data, error } = await client.auth.signInWithPassword({
    email,
    password,
  });
  
  if (error) throw error;
  return data;
}

// Sign out
export async function signOut() {
  const client = getSupabaseClient();
  const { error } = await client.auth.signOut();
  if (error) throw error;
}

// Get current session
export async function getSession() {
  const client = getSupabaseClient();
  const { data: { session }, error } = await client.auth.getSession();
  if (error) return null;
  return session;
}

// Get current user
export async function getCurrentUser() {
  const client = getSupabaseClient();
  const { data: { session }, error: sessionError } = await client.auth.getSession();
  if (sessionError || !session) return null;

  const { data: { user }, error } = await client.auth.getUser();
  if (error) return null;
  return user;
}

// Auth state listener
export function onAuthStateChange(callback: (event: string, session: any) => void) {
  const client = getSupabaseClient();
  return client.auth.onAuthStateChange(callback);
}

// Password reset request
export async function resetPassword(email: string) {
  const client = getSupabaseClient();
  const { error } = await client.auth.resetPasswordForEmail(email, {
    redirectTo: `${window.location.origin}/auth/callback`,
  });
  if (error) throw error;
}

// Update password
export async function updatePassword(newPassword: string) {
  const client = getSupabaseClient();
  const { error } = await client.auth.updateUser({
    password: newPassword,
  });
  if (error) throw error;
}

// Sign in with OAuth (Google)
export async function signInWithGoogle() {
  const client = getSupabaseClient();
  const { data, error } = await client.auth.signInWithOAuth({
    provider: 'google',
    options: {
      redirectTo: `${window.location.origin}/auth/callback`,
    },
  });
  if (error) throw error;
  return data;
}
