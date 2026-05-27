const fs = require('fs');
const { createClient } = require('@supabase/supabase-js');

// Parse .env.local manually
const env = {};
try {
    const envFile = fs.readFileSync('./.env.local', 'utf-8');
    envFile.split('\n').forEach(line => {
        const match = line.match(/^\s*([\w.-]+)\s*=\s*(.*)?\s*$/);
        if (match) {
            let value = match[2] ? match[2].trim() : '';
            if (value.startsWith('"') && value.endsWith('"')) value = value.slice(1, -1);
            env[match[1]] = value;
        }
    });
} catch (e) {
    console.error(e);
}

const supabase = createClient(env.NEXT_PUBLIC_SUPABASE_URL, env.SUPABASE_SERVICE_ROLE_KEY);

async function run() {
    console.log("Updating database to match deterministic tenant model...");

    const deviceName = "GETAI-QCSQC";
    const targetTenantUuid = "d5eb9cf6-8a59-4552-9d97-482f46eaea53";
    const llm_node_id = "00000000-0000-0000-0000-000000000002";

    // 1. Insert into tenants table first
    console.log(`Inserting tenant '${targetTenantUuid}' into 'tenants' table...`);
    const { data: tenantData, error: tenantError } = await supabase
        .from('tenants')
        .upsert({ id: targetTenantUuid, name: `${deviceName} Tenant` })
        .select();

    if (tenantError) {
        console.error("Error inserting into tenants:", tenantError);
        return;
    }
    console.log("Tenant inserted successfully:", tenantData);

    // 2. Update devices table
    console.log(`Updating device '${deviceName}' tenant_id to '${targetTenantUuid}'...`);
    const { data: updatedDevice, error: devError } = await supabase
        .from('devices')
        .update({ tenant_id: targetTenantUuid })
        .eq('device_name', deviceName)
        .select();

    if (devError) {
        console.error("Error updating devices table:", devError);
        return;
    }
    console.log("Device updated successfully:", updatedDevice);

    // 3. Clean old node if it exists
    await supabase.from('nodes').delete().eq('id', llm_node_id);

    // 4. Upsert LLM node under new tenant UUID
    console.log(`Upserting LLM config node under tenant UUID '${targetTenantUuid}'...`);
    const defaultLLMNode = {
        id: llm_node_id,
        tenant_id: targetTenantUuid,
        name: "LLM Configuration",
        type: "skill",
        subtype: "llm_config",
        content: {
            provider: "openai",
            base_url: "https://integrate.api.nvidia.com/v1",
            api_key: "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",
            model: "meta/llama-3.1-8b-instruct"
        },
        is_active: true,
        updated_at: new Date().toISOString()
    };

    const { data: updatedNode, error: nodeError } = await supabase
        .from('nodes')
        .upsert(defaultLLMNode)
        .select();

    if (nodeError) {
        console.error("Error upserting nodes table:", nodeError);
    } else {
        console.log("Node upserted successfully:", updatedNode);
    }
}

run();
