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
    console.log("Querying RLS policies on 'nodes' table...");
    const { data, error } = await supabase.rpc('execute_sql', {
        query_text: "SELECT * FROM pg_policies WHERE tablename = 'nodes';"
    });

    if (error) {
        // Fallback: try raw query if RPC isn't enabled
        console.error("Error calling RPC:", error);
        console.log("Trying direct SQL query using pg_catalog...");
        // If we can't run raw SQL via RPC, let's try reading the tables list or policies list
        return;
    }
    console.log(JSON.stringify(data, null, 2));
}

run();
