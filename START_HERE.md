# Start here (for the team, not for Claude Code)

1. Unzip this folder as `agriledger/` on the computer you will code on (Linux, macOS or WSL is easiest).
2. Install: g++ (C++17), CMake, Docker, PostgreSQL client (`psql`), Node.js (for Playwright).
3. Open a terminal in the folder and start Claude Code: `claude`
4. Install the plugins, one line each:
   - `/plugin install frontend-design@claude-plugins-official`
   - `/plugin install feature-dev@claude-plugins-official`
   - `/plugin install code-review@claude-plugins-official`
   - `/plugin install security-guidance@claude-plugins-official`
   - `/plugin install context7@claude-plugins-official`
   - Playwright (browser testing): `/plugin install playwright@claude-plugins-official`
   If a name is not found, type `/plugin` and search for it.
5. Paste the **Stage 0** prompt from `docs/IMPLEMENTATION_SPEC.md` §12. Read the answer. Fix anything it got wrong. Then approve and paste Stage 1, and so on, one stage at a time.
6. When the Gate 2 document is shared, put it in `docs/` and tell Claude Code to read it.

Before the demo: a teammate checks the quality limits in `db/seed_reference.sql` against the current official e-NAM / DMI document and fills the missing ones.
