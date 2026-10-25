# Empirical chess branching and game length

This is a descriptive research artifact, not a change to the bughouse engine or a choice of its search design. The full interpretation and results are in [the education log](../../education-notes.md).

## Source and population

- Source: Mark Crowther's [The Week in Chess archive](https://theweekinchess.com/twic).
- Fixed issues: 1628, 1631, 1634, 1637, 1640, 1643, 1646, 1649, 1652, 1655, 1658, 1661. These are every third issue from 19 January through 7 September 2026, not a random sample of all chess games.
- Main cohort: both PGN Elo tags at least 2600. Sensitivity cohort: both at least 2700. Ratings are used as provided, not independently verified or assumed comparable across time controls.
- All qualifying records in these issues are considered. Time controls are not restricted; reported event and TimeControl tags are retained. This is not a classical-only dataset.
- Exclude missing/invalid ratings, below-cutoff ratings, unknown results, variants/custom initial positions, parser errors, zero-move games, illegal mainline moves, and duplicate records. Deduplication uses player names, date, event, round, result, and mainline UCI moves; it will not detect all inconsistently labeled duplicates.
- Raw ZIP files are cached in ignored `raw/`. TWIC describes its magazine as free for personal use only; raw archives are not included in versioned outputs. `manifest.json` records download URLs, byte counts, SHA-256 checksums, encodings, sample flow, and library versions.

## Measurement definitions

For game $i$, $L_i$ is the number of recorded mainline plies, counted by replaying the moves. The full-move equivalent is $L_i/2$, which may be a half-integer. This avoids the off-by-one ambiguity in the final printed move number when White makes the last move.

For each pre-move position $s_{i,k}$, where $0\le k<L_i$, count $b_{i,k}=|\mathcal A(s_{i,k})|$ using `python-chess`. Count legal moves, not pseudo-legal moves, distinct destinations, or moves observed in the database. Include each occurrence, even if another game reaches the same position. Exclude the final post-game position from this distribution, since there is no next recorded move. Legal-move counts do not include draw claims or resignation as actions.

`branching_ply_pmf.csv` estimates the distribution of legal-move counts conditional on a recorded game continuing beyond ply $k$. Sample size falls with $k$; this is survival selection, not a sample of all tree nodes at depth $k$. Plot percentile bands describe spread across positions, not uncertainty/confidence intervals for the mean. They are plotted only at plies with at least 30 observations; the CSV retains all plies.

The overall branching distribution weights position occurrences equally, so longer games contribute more observations. The summary additionally reports the mean of per-game mean branching factors, which weights games equally. Games from the same players/events and positions from the same game are correlated; no independent-sample confidence interval is claimed.

## Optional phase heuristic

Compute remaining material units across both players as $M=N+B+2R+4Q$, where letters denote piece counts (pawns and kings have weight zero). The initial value is 24.

1. Endgame: $M\le8$, regardless of ply.
2. Opening: otherwise, $k<20$ (first ten full moves).
3. Middlegame: all remaining positions.

These are reproducible analysis bins, not an authoritative chess phase definition. Promotions can increase material units, so phase labels need not be monotonic. The by-ply distributions do not depend on the phase heuristic. Raw material units are also retained for regrouping.

## What the branching/depth proxies mean

For each game compute:

$$
A_i=L_i\log_{10}\left(\frac{1}{L_i}\sum_{k=0}^{L_i-1}b_{i,k}\right),
\qquad
G_i=\sum_{k=0}^{L_i-1}\log_{10}b_{i,k}.
$$

Thus $10^{A_i}=\bar b_i^{L_i}$ is the constant-average branching proxy, and $10^{G_i}=\prod_k b_{i,k}$ is a varying-branching product along the played path. Also report the geometric mean $10^{G_i/L_i}$. By AM–GM, $G_i\le A_i$.

Neither proxy measures a complete game tree or bounds its size. A played path provides no branching counts for its unplayed siblings, and recorded game length is not the height of the full tree. The path is chosen by players, not uniformly at random among legal actions. Do not interpret these products as an unbiased Monte Carlo tree-size estimate. These are distributions of proxies over games, not distributions of independently sampled $B$ and $D$.

For a directly measured local comparison, sample 20 distinct FENs from each phase (60 total, fixed seed 20261004) from retained position occurrences after within-phase FEN deduplication. Enumerate legal move sequences of lengths 2 and 3 (`perft`) and compare the depth-3 count $C_3(s)$ with $b(s)^3$. Equal phase sample sizes deliberately differ from the observed phase frequencies. These are local legal-sequence counts, not unique positions, and do not stop for repetition or move-count draw adjudication. FEN also does not retain repetition history. A checkmate/stalemate before the requested depth contributes zero sequences at that depth.

## Files

- `games.csv`: per-game metadata, exact lengths, average branching, and logarithmic proxies.
- `positions.csv.gz`: each pre-move position, ply, legal count, material units, phase, check status, FEN, and game ID.
- `game_length_pmf.csv`: exact length counts, probabilities, and empirical CDF.
- `branching_ply_pmf.csv`, `branching_phase_pmf.csv`: full discrete conditional distributions.
- `branching_by_ply.csv`, `branching_by_phase.csv`: means, standard deviations, extrema, and percentiles (NumPy's default linear interpolation).
- Files ending in `_2700.csv` repeat these distributions for the stronger subset with both ratings at least 2700; `rating_sensitivity.png` compares the nested cohorts.
- `local_trees.csv`: actual depth-2/3 legal-sequence counts and comparison with $b^3$.
- `summary.json`, `manifest.json`: numerical summaries and provenance.
- `distributions.png` / `.pdf`, `length_cdf_and_support.png`, `local_trees.png` / `.pdf`: standalone figures.

## Reproduction and validation

Use Python 3.12 with `chess==1.11.2`, NumPy, pandas, and matplotlib; exact versions used are in `manifest.json`. The local `.venv` uses existing system scientific packages and a separately installed chess package. From the repository root on Windows:

```powershell
& 'notes/search/empirical/.venv/Scripts/python.exe' notes/search/empirical/analyze.py --download
& 'notes/search/empirical/.venv/Scripts/python.exe' notes/search/empirical/rating_sensitivity.py
```

The script reuses cached archives; without `--download`, it fails if any archive is missing. It writes only analysis outputs in its own directory. Validation checks initial-position perft counts 20, 400, and 8,902; checkmate and odd-length fixtures; legality of each retained mainline move; correspondence of game lengths to position counts; 20 legal moves in every initial position; and reconciliation of input records with exclusions and retained games. This validates the analysis plumbing, not the local bughouse move generator.

Library references: [PGN replay](https://python-chess.readthedocs.io/en/latest/pgn.html), [legal move generation](https://python-chess.readthedocs.io/en/latest/core.html).
