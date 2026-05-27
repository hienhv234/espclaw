#!/usr/bin/env node
/**
 * DB Reset & Migrate Script
 * Connects directly to Supabase PostgreSQL using the connection string.
 *
 * Usage:
 *   node scripts/db-reset.js [--dry-run]
 *
 * Environment (.env.local):
 *   DATABASE_URL=postgresql://postgres:[PASSWORD]@db.[PROJECT].supabase.co:5432/postgres
 */

const { Client } = require('pg');
const fs = require('fs');
const path = require('path');

// Load env
const envPath = path.join(__dirname, '../neutron-web/.env.local');
if (fs.existsSync(envPath)) {
  const envContent = fs.readFileSync(envPath, 'utf8');
  for (const line of envContent.split('\n')) {
    const trimmed = line.trim();
    if (trimmed && !trimmed.startsWith('#')) {
      const eqIdx = trimmed.indexOf('=');
      if (eqIdx > 0) {
        process.env[trimmed.slice(0, eqIdx)] = trimmed.slice(eqIdx + 1);
      }
    }
  }
}

const DATABASE_URL = process.env.DATABASE_URL;

if (!DATABASE_URL) {
  console.error('❌ DATABASE_URL not found in .env.local');
  process.exit(1);
}

const dryRun = process.argv.includes('--dry-run');
if (dryRun) console.log('🔍 DRY RUN mode — no changes will be made\n');

const migrationsDir = path.join(__dirname, '../supabase/migrations');
const migrationFiles = fs.readdirSync(migrationsDir)
  .filter(f => f.endsWith('.sql') && f !== 'COMPLETE_MIGRATION.sql')
  .sort();

console.log('📋 Migration files (in order):');
migrationFiles.forEach(f => console.log(`   - ${f}`));
console.log('');

async function run() {
  console.log('🔌 Connecting to database...');
  const client = new Client({
    connectionString: DATABASE_URL,
    ssl: { rejectUnauthorized: false },
    connectionTimeoutMillis: 30000,
  });

  try {
    await client.connect();
    console.log('✅ Connected\n');

    // Step 1: Drop all user schemas/tables (CASCADE)
    console.log('⚠️  Dropping all user schemas and objects...');
    if (!dryRun) {
      await client.query(`
        DROP SCHEMA IF EXISTS public CASCADE;
        CREATE SCHEMA public;
        GRANT ALL ON SCHEMA public TO postgres;
        GRANT ALL ON SCHEMA public TO public;
      `);
      console.log('✅ Schema dropped\n');
    } else {
      console.log('🔍 [DRY RUN] Would drop public schema\n');
    }

    // Step 2: Run each migration in order
    for (const file of migrationFiles) {
      const filePath = path.join(migrationsDir, file);
      const sql = fs.readFileSync(filePath, 'utf8');

      console.log(`📄 Running ${file}...`);
      if (!dryRun) {
        try {
          await client.query(sql);
          console.log(`✅ ${file} — OK`);
        } catch (err) {
          console.error(`❌ ${file} — FAILED: ${err.message}`);
          console.error('   The error is above. Fix it and re-run this script.');
          process.exit(1);
        }
      } else {
        console.log(`🔍 [DRY RUN] Would run ${file} (${sql.length} chars)`);
      }
    }

    console.log('');
    if (dryRun) {
      console.log('🔍 DRY RUN complete — no changes made');
    } else {
      console.log('✅ Database reset and migrated successfully!');
    }
  } finally {
    await client.end();
  }
}

run().catch(err => {
  console.error('❌ Fatal error:', err.message);
  process.exit(1);
});
