#pragma once
#include <SQLiteCpp/SQLiteCpp.h>

namespace ircord::db {

// Apply all schema migrations to the database.
// Uses user_version PRAGMA for version tracking.
// Forward-only, non-destructive.
void apply_migrations(SQLite::Database& db);

} // namespace ircord::db
