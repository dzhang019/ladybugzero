"""Reproducible descriptive analysis of strong-player TWIC mainlines.

Run with the local environment: python analyze.py --download
Downloads are cached; reruns use the same bytes, recorded in manifest.json.
No engine source or dependencies are modified.
"""
from pathlib import Path
import argparse
import collections
import hashlib
import io
import json
import math
import os
import platform
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parent
os.environ.setdefault("MPLCONFIGDIR", str(ROOT / ".mplconfig"))
import chess
import chess.pgn
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ISSUES = list(range(1628, 1662, 3))
MIN_ELO = 2600
SEED = 20261004


def perft(board, depth):
    """Legal move sequences, not unique states; ignores draw adjudication."""
    if depth == 0:
        return 1
    if depth == 1:
        return board.legal_moves.count()
    total = 0
    for move in list(board.legal_moves):
        board.push(move)
        total += perft(board, depth - 1)
        board.pop()
    return total


def validate():
    b = chess.Board()
    assert [perft(b, d) for d in (1, 2, 3)] == [20, 400, 8902]
    g = chess.pgn.read_game(io.StringIO('1. f3 e5 2. g4 Qh4# 0-1'))
    assert len(list(g.mainline_moves())) == 4
    assert g.end().board().is_checkmate()
    assert g.end().board().legal_moves.count() == 0
    assert len(list(chess.pgn.read_game(io.StringIO('1. e4 1-0')).mainline_moves())) == 1


def stats(values):
    a = np.asarray(values, dtype=float)
    if not len(a):
        return {"n": 0}
    result = {"n": len(a), "mean": float(a.mean()), "std": float(a.std()),
              "min": float(a.min()), "max": float(a.max())}
    for q in (5, 10, 25, 50, 75, 90, 95, 99):
        result[f"p{q}"] = float(np.percentile(a, q))
    return result


def grouped_stats(frame, by, value):
    return pd.DataFrame([{by: key, **stats(group[value])}
                         for key, group in frame.groupby(by, sort=True)])


def material_phase(board, ply):
    # Explicit descriptive heuristic; not an engine design decision.
    units = sum(weight * len(board.pieces(piece, color))
                for color in chess.COLORS
                for piece, weight in ((chess.KNIGHT, 1), (chess.BISHOP, 1),
                                      (chess.ROOK, 2), (chess.QUEEN, 4)))
    phase = "endgame" if units <= 8 else "opening" if ply < 20 else "middlegame"
    return units, phase


def run(download):
    validate()
    raw = ROOT / "raw"
    raw.mkdir(exist_ok=True)
    games, positions, sources = [], [], []
    excluded = collections.Counter()
    seen = set()
    for issue in ISSUES:
        url = f"https://theweekinchess.com/zips/twic{issue}g.zip"
        path = raw / f"twic{issue}g.zip"
        if not path.exists():
            if not download:
                raise FileNotFoundError(f"Missing {path}; use --download")
            print(f"Downloading {url}", flush=True)
            request = urllib.request.Request(url, headers={"User-Agent": "ChessResearch/1.0"})
            with urllib.request.urlopen(request, timeout=45) as response:
                payload = response.read()
            # Validate before writing a cache entry.
            with zipfile.ZipFile(io.BytesIO(payload)) as archive:
                assert any(n.lower().endswith('.pgn') for n in archive.namelist())
            path.write_bytes(payload)
        payload = path.read_bytes()
        source = {"issue": issue, "url": url, "sha256": hashlib.sha256(payload).hexdigest(),
                  "bytes": len(payload), "records": 0, "accepted": 0}
        with zipfile.ZipFile(io.BytesIO(payload)) as archive:
            for member in archive.namelist():
                if not member.lower().endswith('.pgn'):
                    continue
                data = archive.read(member)
                try:
                    text = data.decode('utf-8-sig')
                    encoding = 'utf-8-sig'
                except UnicodeDecodeError:
                    text = data.decode('cp1252')
                    encoding = 'cp1252'
                source.setdefault('members', []).append({"name": member, "encoding": encoding})
                stream = io.StringIO(text)
                while True:
                    offset = stream.tell()
                    h = chess.pgn.read_headers(stream)
                    if h is None:
                        break
                    source['records'] += 1
                    try:
                        white_elo, black_elo = int(h.get('WhiteElo', '')), int(h.get('BlackElo', ''))
                    except ValueError:
                        excluded['missing_or_invalid_rating'] += 1
                        continue
                    if min(white_elo, black_elo) < MIN_ELO:
                        excluded['below_2600'] += 1
                        continue
                    if h.get('Result') not in ('1-0', '0-1', '1/2-1/2'):
                        excluded['unfinished_result'] += 1
                        continue
                    if h.get('Variant', 'Standard') not in ('Standard', 'Chess') or 'FEN' in h:
                        excluded['variant_or_custom_start'] += 1
                        continue
                    stream.seek(offset)
                    game = chess.pgn.read_game(stream)
                    if game.errors:
                        excluded['parse_error'] += 1
                        continue
                    moves = list(game.mainline_moves())
                    if not moves:
                        excluded['zero_ply'] += 1
                        continue
                    # Preserve independent games that happen to repeat a short line.
                    key = tuple(h.get(k, '') for k in ('White', 'Black', 'Date', 'Event', 'Round', 'Result')) + tuple(m.uci() for m in moves)
                    if key in seen:
                        excluded['duplicate_record'] += 1
                        continue
                    board = game.board()
                    local, bs = [], []
                    valid = True
                    for ply, move in enumerate(moves):
                        legal = list(board.legal_moves)
                        if move not in legal:
                            valid = False
                            break
                        units, phase = material_phase(board, ply)
                        local.append({"ply": ply, "legal_moves": len(legal), "phase": phase,
                                      "material_units": units, "in_check": board.is_check(),
                                      "fen": board.fen()})
                        bs.append(len(legal))
                        board.push(move)
                    if not valid:
                        excluded['illegal_move'] += 1
                        continue
                    seen.add(key)
                    gid = len(games)
                    L = len(moves)
                    sum_log = sum(math.log10(b) for b in bs)
                    final_type = ('checkmate' if board.is_checkmate() else
                                  'stalemate' if board.is_stalemate() else
                                  'insufficient_material' if board.is_insufficient_material() else
                                  'other_recorded_end')
                    games.append({"game_id": gid, "issue": issue, "event": h.get('Event', ''),
                                  "date": h.get('Date', ''), "white": h.get('White', ''),
                                  "black": h.get('Black', ''), "white_elo": white_elo,
                                  "black_elo": black_elo, "min_elo": min(white_elo, black_elo),
                                  "result": h['Result'], "time_control": h.get('TimeControl', ''),
                                  "eco": h.get('ECO', ''), "plies": L, "fullmove_equivalent": L / 2,
                                  "mean_b": float(np.mean(bs)), "geometric_b": 10 ** (sum_log / L),
                                  "log10_mean_b_power_L": L * math.log10(float(np.mean(bs))),
                                  "log10_path_product": sum_log, "final_type": final_type})
                    for row in local:
                        row['game_id'] = gid
                        positions.append(row)
                    source['accepted'] += 1
        sources.append(source)
        print(f"Issue {issue}: {source['records']} records, {source['accepted']} retained; total {len(games)}", flush=True)

    g = pd.DataFrame(games)
    p = pd.DataFrame(positions)
    assert len(g) and len(p) == int(g.plies.sum())
    assert (p.groupby('game_id').size().sort_index().to_numpy() == g.plies.to_numpy()).all()
    assert (p.loc[p.ply == 0, 'legal_moves'] == 20).all()
    assert len(p[p.ply == 0]) == len(g)
    g.to_csv(ROOT / 'games.csv', index=False)
    p.to_csv(ROOT / 'positions.csv.gz', index=False, compression='gzip')
    ply_stats = grouped_stats(p, 'ply', 'legal_moves')
    phase_stats = grouped_stats(p, 'phase', 'legal_moves')
    ply_stats.to_csv(ROOT / 'branching_by_ply.csv', index=False)
    phase_stats.to_csv(ROOT / 'branching_by_phase.csv', index=False)
    for name, cols in [('branching_ply_pmf', ['ply', 'legal_moves']),
                       ('branching_phase_pmf', ['phase', 'legal_moves'])]:
        counts = p.groupby(cols).size().rename('count').reset_index()
        counts['probability'] = counts['count'] / counts.groupby(cols[0])['count'].transform('sum')
        counts.to_csv(ROOT / f'{name}.csv', index=False)
    lengths = g.groupby('plies').size().rename('count').reset_index()
    lengths['probability'] = lengths['count'] / len(g)
    lengths['cdf'] = lengths['probability'].cumsum()
    lengths.to_csv(ROOT / 'game_length_pmf.csv', index=False)

    # A real local tree count provides a check on the constant-b approximation.
    local_rows = []
    for phase in ('opening', 'middlegame', 'endgame'):
        pool = p[p.phase == phase].drop_duplicates('fen')
        sample = pool.sample(n=min(20, len(pool)), random_state=SEED)
        for row in sample.itertuples():
            board = chess.Board(row.fen)
            original_fen = board.fen()
            n2, n3 = perft(board, 2), perft(board, 3)
            assert board.fen() == original_fen
            local_rows.append({"game_id": row.game_id, "ply": row.ply, "phase": phase,
                               "fen": row.fen, "b": row.legal_moves, "perft2": n2,
                               "perft3": n3, "b_cubed": row.legal_moves ** 3,
                               "ratio_perft3_to_b_cubed": n3 / row.legal_moves ** 3,
                               "effective_b3": n3 ** (1 / 3)})
        print(f"Counted actual depth-3 trees: {phase}", flush=True)
    local = pd.DataFrame(local_rows)
    local.to_csv(ROOT / 'local_trees.csv', index=False)
    cohorts = {}
    for cutoff in (2600, 2700):
        subset = g[g.min_elo >= cutoff]
        subset_positions = p[p.game_id.isin(subset.game_id)]
        cohorts[str(cutoff)] = {"games": len(subset), "positions": len(subset_positions),
                               "length_plies": stats(subset.plies),
                               "legal_moves_position_weighted": stats(subset_positions.legal_moves),
                               "legal_moves_game_weighted_mean": float(subset.mean_b.mean()) if len(subset) else None,
                               "log10_mean_b_power_L": stats(subset.log10_mean_b_power_L),
                               "log10_path_product": stats(subset.log10_path_product)}
    summary = {"cohorts": cohorts, "phase_stats": phase_stats.to_dict('records'),
               "endings": g.final_type.value_counts().to_dict(),
               "events": g.event.value_counts().to_dict(), "time_controls": g.time_control.value_counts().to_dict(),
               "result_lengths": grouped_stats(g, 'result', 'plies').to_dict('records'),
               "local_trees": {"perft3": stats(local.perft3),
                               "ratio": stats(local.ratio_perft3_to_b_cubed)},
               "selected_plies": ply_stats[ply_stats.ply.isin([0, 10, 20, 40, 60, 80, 100, 120, 160])].to_dict('records')}
    (ROOT / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    manifest = {"analysis_date": "2026-10-04", "issues": ISSUES, "minimum_both_ratings": MIN_ELO,
                "seed": SEED, "sources": sources, "exclusions": dict(excluded),
                "versions": {"python": platform.python_version(), "chess": chess.__version__,
                             "numpy": np.__version__, "pandas": pd.__version__, "matplotlib": matplotlib.__version__},
                "validation": "Initial-position perft 20/400/8902; mate and odd-length fixtures; every played move legal; position totals and initial counts checked"}
    assert sum(s['records'] for s in sources) == len(g) + sum(excluded.values())
    (ROOT / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    make_plots(g, p, ply_stats, local)
    print(json.dumps({"cohorts": cohorts, "exclusions": dict(excluded), "endings": summary['endings']}, indent=2), flush=True)


def make_plots(g, p, ps, local):
    plt.rcParams.update({'font.size': 10, 'axes.spines.top': False, 'axes.spines.right': False})
    fig, axes = plt.subplots(2, 2, figsize=(13, 9), constrained_layout=True)
    ax = axes[0, 0]
    bins = np.arange(0, int(g.plies.max()) + 11, 10)
    ax.hist(g.plies, bins=bins, weights=np.ones(len(g)) / len(g) * 100, color='#337aab')
    ax.axvline(g.plies.median(), color='#d17826', label=f'Median {g.plies.median():.0f} plies')
    ax.set(xlabel='Recorded game length (plies)', ylabel='Games (%)', title=f'Game lengths: {len(g):,} games, both ratings >= 2600')
    ax.legend()
    ax = axes[0, 1]
    stable = ps[ps.n >= 30]
    ax.fill_between(stable.ply, stable.p10, stable.p90, alpha=.18, color='#337aab', label='10th–90th percentiles')
    ax.fill_between(stable.ply, stable.p25, stable.p75, alpha=.3, color='#337aab', label='25th–75th percentiles')
    ax.plot(stable.ply, stable.p50, color='#174c75', label='Median')
    ax.set(xlabel='Ply from initial position (k)', ylabel='Legal moves before next recorded move', title='Branching conditional on recorded length > k')
    ax.legend(fontsize=8)
    ax = axes[1, 0]
    for phase, color in [('opening', '#337aab'), ('middlegame', '#d17826'), ('endgame', '#34876b')]:
        x = p.loc[p.phase == phase, 'legal_moves']
        counts = x.value_counts().sort_index()
        ax.plot(counts.index, counts / len(x) * 100, label=f'{phase} (n={len(x):,})', color=color)
    ax.set(xlabel='Legal moves', ylabel='Positions (%)', title='Phase-conditioned empirical distributions (heuristic phases)')
    ax.legend(fontsize=8)
    ax = axes[1, 1]
    ax.hist(g.log10_mean_b_power_L, bins=35, alpha=.5, label='L × log10(arithmetic mean b)')
    ax.hist(g.log10_path_product, bins=35, alpha=.5, label='Sum of log10(b) along played path')
    ax.set(xlabel='Base-10 exponent', ylabel='Games', title='Whole-game proxies — NOT measured tree sizes')
    ax.legend(fontsize=8)
    fig.savefig(ROOT / 'distributions.png', dpi=170)
    fig.savefig(ROOT / 'distributions.pdf')
    plt.close(fig)

    fig, axes = plt.subplots(1, 2, figsize=(12, 4.6), constrained_layout=True)
    lengths = np.sort(g.plies)
    axes[0].step(lengths, np.arange(1, len(g) + 1) / len(g), where='post')
    axes[0].set(xlabel='Recorded length (plies)', ylabel='Empirical CDF', title='Fraction of games ending by each length', ylim=(0, 1))
    axes[1].plot(ps.ply, ps.n)
    axes[1].set(xlabel='Ply k', ylabel='Games with a recorded move after ply k', title='Sample size behind branching-by-ply statistics')
    fig.savefig(ROOT / 'length_cdf_and_support.png', dpi=170)
    plt.close(fig)

    fig, axes = plt.subplots(1, 2, figsize=(12, 4.6), constrained_layout=True)
    for phase, color in [('opening', '#337aab'), ('middlegame', '#d17826'), ('endgame', '#34876b')]:
        rows = local[local.phase == phase]
        axes[0].scatter(rows.b_cubed, rows.perft3, label=phase, color=color, alpha=.75)
    low = min(local.b_cubed.min(), local.perft3.min())
    high = max(local.b_cubed.max(), local.perft3.max())
    axes[0].plot([max(low, 1), high], [max(low, 1), high], '--', color='gray')
    axes[0].set(xscale='log', yscale='log', xlabel='b(s)^3 approximation', ylabel='Actual legal sequences at depth 3', title='60 phase-stratified positions; draw rules ignored')
    axes[0].legend()
    ratios = local.ratio_perft3_to_b_cubed
    positive = ratios[ratios > 0]
    axes[1].hist(positive, bins=np.geomspace(positive.min() * .95, positive.max() * 1.05, 26), color='#337aab')
    axes[1].axvline(1, color='#d17826')
    axes[1].set(xscale='log', xlabel='Actual depth-3 count / b(s)^3 (log scale)', ylabel='Sampled positions', title=f'Constant-branching error ({int((ratios == 0).sum())} zero ratios omitted)')
    fig.savefig(ROOT / 'local_trees.png', dpi=170)
    fig.savefig(ROOT / 'local_trees.pdf')
    plt.close(fig)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--download', action='store_true')
    run(parser.parse_args().download)
