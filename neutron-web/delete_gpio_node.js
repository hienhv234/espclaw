const { createClient } = require('@supabase/supabase-js');
const fs = require('fs');
const path = require('path');

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
    console.log("Deleting corrupted gpio_config node from Supabase...");
    const { data, error } = await supabase
        .from('nodes')
        .delete()
        .eq('id', '333b9cf6-8a59-4552-9d97-482f46eaea53')
        .select();

    if (error) {
        console.error("Error deleting database node:", error);
    } else {
        console.log("Successfully deleted corrupted database node!", JSON.stringify(data, null, 2));
    }
}

run();
