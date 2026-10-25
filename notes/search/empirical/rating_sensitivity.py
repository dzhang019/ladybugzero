"""Export full distributions for the stronger 2700+ subset after analyze.py."""
from analyze import ROOT, grouped_stats, np, pd, plt

games = pd.read_csv(ROOT / 'games.csv')
positions = pd.read_csv(ROOT / 'positions.csv.gz')
elite = games[games.min_elo >= 2700]
ep = positions[positions.game_id.isin(elite.game_id)]
assert len(elite) and len(ep) == elite.plies.sum()
by_ply = grouped_stats(ep, 'ply', 'legal_moves')
by_ply.to_csv(ROOT / 'branching_by_ply_2700.csv', index=False)
grouped_stats(ep, 'phase', 'legal_moves').to_csv(ROOT / 'branching_by_phase_2700.csv', index=False)
for name, cols in [('branching_ply_pmf_2700', ['ply', 'legal_moves']),
                   ('branching_phase_pmf_2700', ['phase', 'legal_moves'])]:
    counts = ep.groupby(cols).size().rename('count').reset_index()
    counts['probability'] = counts['count'] / counts.groupby(cols[0])['count'].transform('sum')
    counts.to_csv(ROOT / f'{name}.csv', index=False)
lengths = elite.groupby('plies').size().rename('count').reset_index()
lengths['probability'] = lengths['count'] / len(elite)
lengths['cdf'] = lengths.probability.cumsum()
lengths.to_csv(ROOT / 'game_length_pmf_2700.csv', index=False)

fig, axes = plt.subplots(1, 2, figsize=(12, 4.6), constrained_layout=True)
for cutoff, color in [(2600, '#337aab'), (2700, '#d17826')]:
    g = games[games.min_elo >= cutoff]
    p = positions[positions.game_id.isin(g.game_id)]
    x = np.sort(g.plies)
    axes[0].step(x, np.arange(1, len(x) + 1) / len(x), where='post',
                 label=f'Both >= {cutoff} (n={len(g):,})', color=color)
    ps = grouped_stats(p, 'ply', 'legal_moves')
    ps = ps[ps.n >= 30]
    axes[1].plot(ps.ply, ps['mean'], label=f'Both >= {cutoff}', color=color)
axes[0].set(xlabel='Recorded game length (plies)', ylabel='Empirical CDF', title='Rating cutoff sensitivity (nested samples)')
axes[1].set(xlabel='Ply from initial position', ylabel='Mean legal moves', title='Only plies with at least 30 observations shown')
for ax in axes:
    ax.legend()
fig.savefig(ROOT / 'rating_sensitivity.png', dpi=170)
plt.close(fig)
print(f'Exported distributions for {len(elite)} games / {len(ep)} positions with both ratings >=2700.')
