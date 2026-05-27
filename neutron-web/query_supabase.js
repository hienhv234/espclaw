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
    const { data: nodes, error } = await supabase.from('nodes').select('*');
    if (error) {
        console.error("Error fetching nodes:", error);
        return;
    }
    console.log("=== RAW NODES IN DATABASE ===");
    console.log(JSON.stringify(nodes, null, 2));
}

run();
