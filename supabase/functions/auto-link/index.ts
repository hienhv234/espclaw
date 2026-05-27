// Supabase Edge Function - Auto Link
// Creates links based on detected patterns
// FIXED: uses pattern_id (not auto_link_id) to match schema

import { serve } from "https://deno.land/std@0.168.0/http/server.ts";
import { createClient } from "https://esm.sh/@supabase/supabase-js@2";

const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "authorization, x-client-info, apikey, content-type",
};

serve(async (req) => {
  if (req.method === "OPTIONS") {
    return new Response("ok", { headers: corsHeaders });
  }

  try {
    const supabaseUrl = Deno.env.get("SUPABASE_URL")!;
    const supabaseKey = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!;
    const supabase = createClient(supabaseUrl, supabaseKey);

    const { tenant_id, confidence_threshold = 0.5, dry_run = false } = await req.json();

    if (!tenant_id) {
      return new Response(JSON.stringify({ error: "tenant_id required" }), {
        status: 400,
        headers: { ...corsHeaders, "Content-Type": "application/json" },
      });
    }

    // Validate tenant_id is UUID
    const uuidRegex = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
    if (!uuidRegex.test(tenant_id)) {
      return new Response(JSON.stringify({ error: "Invalid tenant_id format" }), {
        status: 400,
        headers: { ...corsHeaders, "Content-Type": "application/json" },
      });
    }

    // Get high-confidence patterns that are enabled for auto-linking
    const { data: patterns, error: patternsError } = await supabase
      .from("patterns")
      .select("*")
      .eq("tenant_id", tenant_id)
      .eq("auto_link_enabled", true)
      .gte("confidence", confidence_threshold)
      .order("confidence", { ascending: false })
      .limit(50); // Limit to prevent runaway

    if (patternsError) throw patternsError;

    const createdLinks: string[] = [];

    if (!patterns || patterns.length === 0) {
      return new Response(
        JSON.stringify({
          success: true,
          links_created: 0,
          links: [],
          dry_run,
          message: "No patterns found with auto_link_enabled=true",
        }),
        { headers: { ...corsHeaders, "Content-Type": "application/json" } }
      );
    }

    // Process each pattern — find matching nodes and create links
    for (const pattern of patterns) {
      // Find nodes matching node_type_a
      const { data: nodesA } = await supabase
        .from("nodes")
        .select("id, type, name")
        .eq("tenant_id", tenant_id)
        .eq("type", pattern.node_type_a)
        .eq("is_active", true)
        .limit(20);

      // Find nodes matching node_type_b
      const { data: nodesB } = await supabase
        .from("nodes")
        .select("id, type, name")
        .eq("tenant_id", tenant_id)
        .eq("type", pattern.node_type_b)
        .eq("is_active", true)
        .limit(20);

      if (!nodesA?.length || !nodesB?.length) continue;

      // Check for existing links and create new ones
      for (const nodeA of nodesA) {
        for (const nodeB of nodesB) {
          if (nodeA.id === nodeB.id) continue;

          // Check if link already exists
          const { data: existingLink } = await supabase
            .from("links")
            .select("id")
            .eq("tenant_id", tenant_id)
            .eq("source_node_id", nodeA.id)
            .eq("target_node_id", nodeB.id)
            .single();

          if (existingLink) continue;

          const suggestedType = pattern.suggested_link_type || "auto_related";

          if (!dry_run) {
            // Create new link using pattern_id (FIXED: was auto_link_id before)
            const { error: linkError } = await supabase.from("links").insert({
              tenant_id,
              source_node_id: nodeA.id,
              target_node_id: nodeB.id,
              type: suggestedType,
              weight: pattern.confidence,
              confidence: pattern.confidence,
              is_ai_generated: true,
              pattern_id: pattern.pattern_id, // FIXED: was auto_link_id
            });

            if (!linkError) {
              createdLinks.push(
                `${nodeA.name} (${nodeA.type}) → ${nodeB.name} (${nodeB.type})`
              );
            }
          } else {
            createdLinks.push(
              `[DRY RUN] ${nodeA.name} → ${nodeB.name} (confidence: ${pattern.confidence})`
            );
          }
        }
      }
    }

    return new Response(
      JSON.stringify({
        success: true,
        links_created: createdLinks.length,
        links: createdLinks.slice(0, 100), // Limit response size
        dry_run,
        patterns_processed: patterns.length,
        message: dry_run
          ? `Dry run complete — ${createdLinks.length} links would be created`
          : `Created ${createdLinks.length} new links`,
      }),
      { headers: { ...corsHeaders, "Content-Type": "application/json" } }
    );
  } catch (error) {
    return new Response(JSON.stringify({ error: error.message }), {
      status: 500,
      headers: { ...corsHeaders, "Content-Type": "application/json" },
    });
  }
});
