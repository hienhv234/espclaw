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
    console.log("Touching LLM Configuration node to trigger live sync...");
    const llm_node_id = "00000000-0000-0000-0000-000000000002";
    
    const { data, error } = await supabase
        .from('nodes')
        .update({ updated_at: new Date().toISOString() })
        .eq('id', llm_node_id)
        .select();

    if (error) {
        console.error("Error touching node:", error);
    } else {
        console.log("Node touched successfully! New timestamp:", data[0].updated_at);
    }
}

run();
