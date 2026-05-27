const { createClient } = require('@supabase/supabase-js');
const fs = require('fs');
const path = require('path');

// Pure JS .env.local loader
const envPath = path.resolve(__dirname, '.env.local');
if (fs.existsSync(envPath)) {
    const envConfig = fs.readFileSync(envPath, 'utf8');
    envConfig.split('\n').forEach(line => {
        const parts = line.split('=');
        if (parts.length >= 2) {
            const key = parts[0].trim();
            const value = parts.slice(1).join('=').trim().replace(/^["']|["']$/g, '');
            process.env[key] = value;
        }
    });
}

const supabaseUrl = process.env.NEXT_PUBLIC_SUPABASE_URL;
const supabaseKey = process.env.SUPABASE_SERVICE_ROLE_KEY;

if (!supabaseUrl || !supabaseKey) {
    console.error("Missing Supabase credentials!");
    process.exit(1);
}

const supabase = createClient(supabaseUrl, supabaseKey);

async function run() {
    console.log("Updating LLM Configuration model to 'gemini-flash-latest'...");
    const { data, error } = await supabase
        .from('nodes')
        .update({
            content: {
                provider: "custom",
                base_url: "https://generativelanguage.googleapis.com/v1beta/openai",
                api_key: "AIzaSyDMXjbepBCp1FZw5fuoK6ZzHXPyZvbr5FA",
                model: "gemini-3.1-flash-lite"
            },
            updated_at: new Date().toISOString()
        })
        .eq('id', '00000000-0000-0000-0000-000000000002')
        .select();

    if (error) {
        console.error("Error updating database:", error);
    } else {
        console.log("Successfully updated database node to 'gemini-flash-latest'!", JSON.stringify(data, null, 2));
    }
}

run();
