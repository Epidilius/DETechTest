# Part B: written answers

## Task 1: the table structure

The diagram is in [er-diagram.svg](er-diagram.svg), and the DDL is in [schema.sql](schema.sql).

### What the data showed

I ran a quick profile on all 15,286 documents before designing anything. The structure follows what is in the data.

| Finding | What the schema does |
|---|---|
| There are 10,000 inventory and 5,286 rewards documents. Inventory documents come in 24 field combinations, and 3,959 hold nothing beyond ids, a date, and the cross-play flag. | Every column of `inventory_update` except the ids and the date allows null. |
| `cross_play_enabled` is the text `"true"` or `"false"` in 9,895 documents and absent from 105. | The column is a boolean. Absent becomes null, and any other text rejects the document. |
| `game_mode_id` is a 64-bit number in all 3,699 documents that have it, with around half being negative. The brief's example shows a small integer. | The column is `bigint`. |
| Rewards documents carry a `date`, and inventory documents a `mission_id`. Neither is in the brief's notes. | Both are stored. |
| An `increment_exp` key is always `category:sku:count`. Each category has its own SKU range inside 1 to 300, and rewards use the same range. | One `sku` table serves both, and the SKU alone identifies an item. I dropped the `count` suffix, but am unsure if this was correct. I don't know why the word is on every entry, and would have preferred to ask someone about this. |
| All 32,494 `increment_exp` values are whole numbers, even if three categories store them as doubles. 80 are negative, all in `misc`. | `amount` is `bigint`. Negative values are kept. A fraction rejects, as rounding would change a player's experience without anyone knowing. |
| 11 squads list the same player twice, and 2 squad members own no document of their own. | `squad_member` is keyed by position in the array, and `player` holds every id seen. |
| Every `rewards_id` points to exactly one rewards document, with the same player and the same timestamp. No reward is pointed to twice. | An update links to at most one reward, and the link is unique. |
| SKU 229 appears only as a reward, so its category is unknown. | `sku.category_id` allows null, and is filled in if an `increment_exp` key later names that SKU. |

### Choices

- **Partitioned by day.** `reward`, `inventory_update`, `exp_increment`, and `squad_member` each have one partition per UTC day.
- **Keys start with `logged_at`.** PostgreSQL requires the partition column in a partitioned table's primary key. The child tables therefore carry their parent's `logged_at` as well as its id.
- **Ids are 12 raw bytes.** A MongoDB ObjectId stored as `bytea` takes 13 bytes. As hex text it takes 25. Every large table repeats these ids in its rows and its indexes.
- **Small lookup tables for mission state, join method, and category.** A value the script has not seen before gets a new row, so the schema does not change when the game adds one.
- **Acceptable losses.** An empty `increment_exp` (283 documents) and a missing one both become zero rows.

### Alternative Considerations

Plain tables keyed by the document id alone would be simpler, and is the natural shape for ~15k documents. They would not hold up at the volume in question 3, where removing old data means deleting billions of rows and every load writes into indexes that cover years. 

## Task 2: the script

How to run it, and what it does step by step, is in [README.md](README.md). These are its limits.

- **It reads by `_id`.** A run continues after the last `_id` it loaded. A document written late with an older `_id` is missed until a run with `--from-start`.
- **A reward must be loaded before its update.** An update whose reward is not in PostgreSQL with the same timestamp is rejected. In the sample every reward has a lower `_id` than its update. In any other environment, the script would need to handle this edge case.
- **Fields it does not know are ignored.** A new field in MongoDB is not loaded and is not reported.
- **Rejected documents go to a file.** At scale they should go to a table, so they can be investigated, fixed, and loaded.
- **Partitions are created during the load.** Creating a partition briefly locks the parent table. A production system would create them ahead of time.

## Question 3: storing this data in a relational database

The volume drives most of these decisions. If production has the same mix as the sample, 25 million documents a day becomes ~91 million rows a day, and ~33 billion rows a year. Measured on the sample, PostgreSQL needs about 517 bytes per source document including indexes, which is about 13 GB a day and 4.7 TB a year. A table design that is correct but treats this as one growing table will not survive, so the design has to actively support this volume.

- **Partition by time.** The data only ever arrives in time order and is never updated. With one partition per day, a load writes to one small partition, a query for a date range skips every other one, and old data is removed by dropping a partition, which is instant. Deleting the same rows would be slow and would leave the table bloated.
- **Retention should be decided asap.** "Several years" should be a concrete number. Recent partitions can stay on fast storage, while older ones are moved to cheaper storage, compressed, or exported and dropped.
- **Keep rows narrow and indexes few.** At billions of rows, every byte and every index is paid for on each insert. This is why ids are 12 raw bytes, lookup values are 2-byte ids, and the large tables have only their primary key plus one extra index.
- **Load in bulk, and make loads repeatable.** Rows go in with `COPY`, a batch at a time, in one transaction per batch. A batch that fails can be run again without creating duplicates.
- **Expect the source to change.** MongoDB enforces no structure. The sample already has some discrepancies with the notes. The load has to check every document and report what it refuses.
- **A row store is the wrong tool for the long history.** Queries sampling years of data, such as experience earned per warframe per month, read a few columns of billions of rows. Daily summary tables answer the common questions efficiently. For queries the summaries cannot cover, a columnar warehouse fed from the same load would serve better than PostgreSQL. PostgreSQL can keep the recent data according to the decided on retention policy.
- **One server has a ceiling.** Past a few terabytes, backups, restores, and upgrades become slow and risky. Read replicas help with queries. Other choices include sharding or keeping less history in PostgreSQL.

## Question 4: validating the data

Validation happens through four tests.

**Pre Design: profile the source.** Count how often each field is present, the types it holds, and its range of values, and then verify that every link points at something. This is where the findings in the table above came from. A schema designed from the task notes alone would have used a 32-bit `game_mode_id` and would have failed on the first load.

**Schema Design: state what must be true as constraints.** Foreign keys, `NOT NULL`, the unique reward link, the 12-byte check on ids, and the check that `time_alive_s` is not longer than `total_mission_time_s` are all enforced by PostgreSQL. If the data ever breaks one of them, the load fails loudly and does not store invalid or incorrect data.

**Script Runtime: check each document, and never repair it silently.** A document with invalid data (fraction for experience, malformed key, missing id) is rejected with a reason and counted. No data is modified or inferred. The transform has 32 checks of its own, and the load has another 30.

**Post Load: reconcile the two databases with separate code.** `validate.py` measures MongoDB and PostgreSQL independently, using none of the transform code, and compares 22 figures: document counts, distinct players and SKUs, reward links, cross-play counts, total experience overall and per category, and updates per mission state. All 22 match for the sample. Totals matter more than counts here, because a halved or doubled amount leaves every row count unchanged. Running the load a second time and seeing zero rows inserted proves it can be repeated safely.

At the volume in question 3 the same ideas apply per day and are automated:

- Reconcile each day's partition against MongoDB with aggregations run on each server
- Watch rates: the share of rejected documents, the share of nulls per column, and any new mission state, join method, or category. A sudden change in any of them means the source changed.
- Compare a random sample of documents field by field each day, which catches errors that totals can hide.
- Alert on a day with less data than usual, since a load that silently reads nothing passes every other check.

## Question 5: fixing accounts after the half-experience bug

My answer to this question is "it depends". The short version is the same approach I would take to any bug: investigate, fix, remediate. With a large team, I might not be able to do each step. I can notice the data is malformed coming in, but would I be able to determine why, or be able to resolve it myself? The worst case is that all I can do is escalate the issue to the correct team. My ideal process is:

**1. Investigate** With the game team, establish when the patch went live, which platforms and builds were affected, if there are any other affected players besides cross-save players, and how much exp they are owed.

**2. Fix** The set of affected players will continue to grow until the bug is fixed. This might mean downgrading the game version, pushing a new hotfix, or a temporary (worst case, "There is nothing so permanent as the temporary") data solution that transforms the malformed data into proper amounts.

**3. Validate** Before trusting everything,  we would want to compare warframe experience for the suspected updates with a control group, such as the same players before the hotfix. The suspect group should earn half as much. If they don't, someone got something wrong, and "fixing" accounts would make things worse.

**4. Data Investigation** A player can have several inventory updates in the bugged window, so the amount owed is the sum of all these. If the logged amount is the halved amount that was granted, the experience owed equals the amount logged:

```sql
SELECT inventory_update.player_id, exp_increment.sku_id, sum(exp_increment.amount) AS experience_owed, count(*) AS updates
FROM game_log.inventory_update
JOIN game_log.exp_increment USING (logged_at, inventory_update_id)
JOIN game_log.sku USING (sku_id)
JOIN game_log.item_category USING (category_id)
WHERE inventory_update.logged_at >= :bug_went_live
  AND inventory_update.logged_at < :bug_was_fixed
  AND inventory_update.cross_play_enabled
  AND item_category.name = 'warframe'
  AND exp_increment.amount > 0
GROUP BY inventory_update.player_id, exp_increment.sku_id;
```

**5. Data Verification.** The results get written to a table to be reviewed. One row per player and warframe, with the amount owed, the updates it came from, and an id for this fix. This table is the record of what was done and why. It is also what makes the fix safe to run multiple times, as each row is flagged once it has been applied.

**6. Dry Run.** Report how many players are affected, the total owed, and the largest individual amounts. Someone other than me validates the output and signs off on the numbers.

**7. Remediation.** Experience gain has side effects that the game code already handles. A script that updates or writes to the database could bypass these. Grant in small batches, starting with a few internal accounts, at a rate the service can absorb, and mark each row as it is applied. If the run stops it can resume without double dipping.

**8. Verify.** Every row is marked as applied exactly once. The affected accounts show the expected experience. The new grants appear in the logs. Support tickets about missing experience stop coming in.

**9. Keep a Downgrade Path.** Since every grant is recorded per player and warframe, a mistake can be reversed precisely if required. At this point, any mistakes should have been caught, but being safe is important.

**10. Prevention.** Add a monitor (or adapt an existing one) that will record and evaluate experience gain rates, so a future halving (or doubling) is noticed quickly and automatically and not through player reports.
