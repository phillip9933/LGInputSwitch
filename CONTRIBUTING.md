# Contributing to LGInputSwitch

Read README.md for setup and supported hardware. Keep changes focused, preserve licenses/notices and use synthetic or redacted reports. Include verification and disclose material AI assistance.

## Public roadmap and issue management

Follow the [public roadmap](https://github.com/users/phillip9933/projects/2) for release plans and the work board for individual tasks. This is the shared convention across maintained repositories. Issues, milestones and Project fields are the live source of truth; do not duplicate the live task list in ROADMAP.md or other Markdown backlogs.

### Issue type and priority

Every issue has exactly one purpose label: `type:bug` (existing behavior is broken), `type:feature` (new or changed user-facing behavior), `type:maintenance` (refactoring, dependencies, CI, performance or debt), `type:docs` (documentation only), or `type:release` (release tracking). Choose the purpose: a bug requiring refactoring is still a bug.

After triage, use exactly one priority label: `priority:urgent` (immediate action, such as active data loss or serious security impact), `priority:high` (next work; significant impact), `priority:normal` (default accepted work), or `priority:low` (worthwhile but not currently important). Priority means work order, not a promised date. Keep suspected security vulnerabilities out of public issues until safe disclosure; use the repository’s private security-reporting channel.

### Work status

Use the Project’s **Status** field, never status labels: **Inbox → Ready → In progress → Done**, with **Blocked** available when work cannot proceed. Inbox needs assessment; Ready is accepted and sufficiently defined; In progress is active work. A Blocked issue records its dependency and what unblocks it. Done means its acceptance criteria are met with verification evidence, not necessarily shipped. Close rejected or duplicate work with the appropriate reason; do not mark it delivered or automatically move every closed issue to Done.

### Release targets

Repository milestones identify intended stable versions, with a platform prefix where useful (for example, Android or Desktop). Assign a milestone only when inclusion is intended; unscheduled ideas have none. Alpha, beta and RC builds stay under the intended stable-version milestone. Cross-repository release trackers link the relevant repository milestones. Estimated start/target dates are planning aids, not commitments; leave unknown dates unset.

Multi-platform repositories use `platform:android`, `platform:desktop` or `platform:shared`. Do not add redundant platform labels to single-platform repositories.

### Release maturity and promotion gates

Only `type:release` issues use the Project’s separate **Stage** field: **Planned → Alpha → Beta → RC → Stable**. Each release has one named owner.

- **Planned → Alpha:** record scope, acceptance criteria and supported platforms; an installable build exists (a library supplies a consumable bundle and installable test host).
- **Alpha → Beta:** intended release features are implemented, core workflows pass, and remaining limitations are documented.
- **Beta → RC:** release-blocking bugs are resolved; required regression, upgrade/migration and data-safety tests pass; freeze scope.
- **RC → Stable:** validate the final candidate; prepare release notes and known limitations; verify signed/checksummed artifacts where applicable; publish the release.

For wallets and cloud clients, explicitly test relevant encryption, storage, backup/restore and sync behavior. Promote on evidence, never because a date arrived. Stable means the release was actually published. If new feature scope enters after RC, return the release to Beta and repeat affected checks. GitHub displays these fields and checklists; it does not automatically enforce release gates.

### Issue contents and public views

Every issue records its goal/problem, scope and explicit exclusions, a **Done when** acceptance checklist, dependencies (or None), and verification results/links before closure. New reports may say verification is pending. Release trackers additionally name the target version, one owner, included issues, promotion-gate checklists, remaining blockers and build/release links.

The **Roadmap** view contains only `type:release` items on a timeline, with Stage and estimated start/target dates. The **Work board** contains bugs, features, maintenance and docs grouped by Status. Keep statuses and stages current as work changes. Preserve report authors’ original details when adding triage structure, and disclose material AI assistance in implementation and verification summaries.
