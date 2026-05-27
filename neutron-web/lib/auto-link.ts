/**
 * Fix for auto-link/index.ts
 * Bug: was inserting `auto_link_id` column but schema has `pattern_id`
 * Fix: changed to `pattern_id` to match the schema
 */
export async function autoLink(
  tenantId: string,
  confidenceThreshold = 0.5,
  dryRun = false
): Promise<{
  success: boolean;
  links_created: number;
  links: string[];
  dry_run: boolean;
  message: string;
}> {
  // NOTE: This function is now replaced by supabase/functions/auto-link/index.ts
  // which correctly uses `pattern_id` instead of `auto_link_id`
  return {
    success: false,
    links_created: 0,
    links: [],
    dry_run: dryRun,
    message: 'Deprecated: use supabase/functions/auto-link endpoint instead',
  };
}
