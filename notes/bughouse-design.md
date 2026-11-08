# Bughouse engine design decisions

## State and value

- Model two teams on two boards. Team 1 plays White on A and Black on B. Team 2 has the opposite colors.
- The game state includes both positions, pockets, side to move on each board, castling and en passant rights, and the history needed to apply repetition rules.
- Measure value from Team 1’s perspective. Team 1 maximizes and Team 2 minimizes. Checkmating Team 2 on either board gives $+1$. Checkmating Team 1 gives $-1$.

## Search and move order

- Use one search tree over both boards per engine. The tree evaluates moves for both teams. In shared-tree self-play, it informs both submission decisions. In match play, the engine submits only for its own team.
- When both board turns belong to one team, that team chooses the next move from either board. At mixed positions, either team can submit. The shared-tree model uses search work as its clock, in blocks of 100 MCTS simulations.
- At each checkpoint, both teams decide simultaneously whether to submit or wait. A sole submission executes. If both submit, each move has a 50% chance of executing. If both wait before checkpoint $k$, run another 100 simulations on the same tree. If both wait at checkpoint $k$, choose a team with equal probability and execute its best current move.
- Keep separate estimates $X$ and $Y$ for the two teams’ best available moves, both in Team 1’s units. For optimal stopping, compare the outcomes of submitting now with the expected continuation value $W$ if both wait, including later submission decisions. If $X>Y$, both submit regardless of $W$. If $X<Y$, Team 1 submits when $W<X$, Team 2 submits when $W>Y$, and both wait when $X<W<Y$.

## Open decisions

- How to allocate simulations between the teams’ moves and how to value waiting.
- The value of the checkpoint cap $k$ and how to order submissions in external matches.
