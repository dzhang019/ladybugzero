# Search from first principles

Date: 2026-09-28

Section 3 expanded: 2026-10-04.

This note develops the mathematical problem search is trying to solve, then explains why an algorithm like Leela's makes sense. We start with ordinary chess: its strict alternation makes the foundations cleaner. We then identify which assumptions bughouse changes. This is a conceptual introduction, not an agreed bughouse search design.

## 1. A game defines a value; search tries to compute it

Initially, assume a deterministic, finite, two-player, zero-sum game with perfect information and alternating turns.

Write:

- $s\in\mathcal S$ for a state.
- $\mathcal A(s)$ for its legal actions.
- $T(s,a)$ for the successor state after action $a$.
- $z(s)\in\{-1,0,+1\}$ for the payoff at a terminal state, from the perspective of the player to move there.

A state must contain everything needed to determine legal actions, transitions, and outcomes. For chess, piece placement alone is insufficient: castling rights, en passant, and relevant history matter too.

Define $V^*(s)$ as the outcome the player to move can guarantee against optimal opposition. Then

$$
V^*(s)=
\begin{cases}
z(s), & s\text{ terminal},\\[4pt]
\displaystyle\max_{a\in\mathcal A(s)}
\left[-V^*(T(s,a))\right], & \text{otherwise}.
\end{cases}
$$

This is the **negamax recurrence**.

The minus sign is a change of perspective. If a successor is winning for the opponent, reaching it is losing for us. Zero-sum payoffs justify negation; strict alternation makes it necessary on every transition.

Define the corresponding action value:

$$
Q^*(s,a)=-V^*(T(s,a)).
$$

The decision we actually need is

$$
a^*\in\arg\max_a Q^*(s,a).
$$

Notice the distinction: **choosing the best move does not necessarily require accurately evaluating every move.** This is central to efficient search.

## 2. The difficulty is computation, not defining optimal play

The recurrence already specifies a solution. Expand the entire game tree, evaluate its terminal leaves, and work backward.

But with roughly $b$ actions per state and depth $d$, a full expansion contains on the order of $b^d$ nodes. Even modest values make exhaustive computation impractical.

So we introduce a computational budget $B$. A search algorithm must decide which parts of the tree deserve that budget and eventually return a move $\hat a_B$.

One mathematical objective is to minimize the decision error

$$
R_B(s)=V^*(s)-Q^*(s,\hat a_B).
$$

This is called *simple regret*. It measures how much value we lose by choosing the final move.

We cannot generally calculate this quantity during search, because $Q^*$ is precisely what we do not know. Nevertheless, it clarifies the purpose of search: allocate computation to improve the final decision.

## 3. Evaluation lets us stop before the game ends

Suppose we reach a nonterminal state $s_L$ and cannot afford to expand it further. We need an estimate of its value.

### 3.1. First choose a continuation policy and evaluate it

One possibility is to finish the game using a specified policy $\pi$, obtaining a terminal result $Z$. Here $\pi$ specifies continuation play for both players, and $Z$ is expressed from the perspective of the player to move at $s_L$. Repeating this gives samples estimating

$$
V^\pi(s_L)=\mathbb E_\pi[Z\mid s_L].
$$

That is the Monte Carlo idea: estimate an expectation using samples.

But there is a crucial distinction:

$$
\boxed{V^\pi(s)\ \text{need not equal}\ V^*(s).}
$$

Winning frequently against a particular continuation policy does not imply winning against optimal defense.

For example, a move might win if the opponent chooses any of nine replies and lose against the tenth. Uniformly sampling replies gives expected payoff

$$
\frac{9(+1)+1(-1)}{10}=0.8.
$$

Its optimal-play value is nevertheless $-1$, assuming the opponent can force the loss with that tenth reply.

Consequently, search must improve **the opponent's decisions as well as our own**. Averaging random games alone does not solve the adversarial problem.

Before introducing a neural evaluator, we need to explain what we can actually do about this problem.

### 3.2. Separate measuring a policy from improving its decisions

The quantity $V^\pi(s)$ is the exact expectation under $\pi$. Its Monte Carlo estimate from $n$ independent continuations is

$$
\widehat V_n^\pi(s)=\frac{1}{n}\sum_{j=1}^{n}Z_j.
$$

Relative to optimal play, the error decomposes as

$$
\widehat V_n^\pi(s)-V^*(s)
=\underbrace{\widehat V_n^\pi(s)-V^\pi(s)}_{\text{sampling error}}
+\underbrace{V^\pi(s)-V^*(s)}_{\text{policy mismatch}}.
$$

This gives us two different things to work on:

- **Sampling error:** run more independent continuations under the same policy. This measures that policy's consequences more accurately.
- **Policy mismatch:** reconsider the decisions made during the continuation. Repeating the same policy more often does not correct its mistakes.

For deterministic play in a deterministic game, every rollout from the same state is identical. One completed rollout already determines $V^\pi(s)$; further identical rollouts contribute nothing. Yet the policy mismatch can still be large.

We would like a stronger continuation policy, but “first find a good policy” is not yet an algorithm. Finding good moves is the problem we started with. We need a concrete way to use our current, imperfect policy to help choose better moves.

### 3.3. The first concrete step: try each first move, then follow the policy

Take a fixed baseline policy $\pi_0$. It could choose uniformly among legal moves, or use some existing heuristic. It does not have to be optimal; it only needs to define how both sides continue playing.

At the current position $s$:

1. Choose a candidate legal action $a$ to investigate.
2. Apply it, reaching $s'=T(s,a)$.
3. Finish games from $s'$ using $\pi_0$ for both sides.
4. Convert their outcomes to the perspective of the player choosing $a$ at $s$.
5. Repeat for the other candidate actions, then choose the highest estimated value.

Mathematically, forcing the first action and using $\pi_0$ afterward gives

$$
Q^{\pi_0}(s,a)=-V^{\pi_0}(T(s,a)),
$$

and our decision rule becomes

$$
\hat a(s)\in\arg\max_{a\in\mathcal A(s)}\widehat Q^{\pi_0}(s,a).
$$

We have now constructed a move-selection procedure using an imperfect policy. It answers: **which first move looks best if subsequent play follows $\pi_0$?** It does not yet answer which move is best against optimal defense.

There is a precise, limited improvement statement here. If the action values are known exactly, then

$$
V^{\pi_0}(s)
=\sum_a\pi_0(a\mid s)Q^{\pi_0}(s,a)
\le\max_a Q^{\pi_0}(s,a).
$$

Choosing the best first action cannot be worse than averaging over the baseline's first-action choices, **when both sides follow the unchanged baseline afterward**. With estimated action values, sampling error can also cause us to select the wrong maximizer.

### 3.4. Next, stop assuming that the opponent follows the baseline immediately

The previous procedure can still prefer a trap that works only when the opponent misses a defense. To investigate that, expand the opponent's replies explicitly.

Consider a tiny illustrative game. In this diagram, all terminal values are from the **root player's perspective**, so the opponent minimizes them:

```text
Our move
  A -> opponent has ten replies
         nine replies end in a win for us: +1
         one reply ends in a loss for us:  -1

  B -> every opponent reply ends in a draw: 0
```

With uniform random continuation at the opponent's turn, the first-move procedure sees

$$
Q^{\pi_0}(s,A)=0.8,\qquad Q^{\pi_0}(s,B)=0,
$$

so it chooses A. More rollouts only make the estimate $0.8$ more precise.

If we explicitly examine all opponent replies, we instead obtain

$$
Q^*(s,A)=\min\{+1,\ldots,+1,-1\}=-1,
\qquad Q^*(s,B)=0.
$$

Now we choose B. **The correction came from treating the opponent's reply as an adversarial choice, rather than averaging it according to a weak policy.**

In this tiny example, the replies end the game, so their values are exact. In chess, most replies lead to another nonterminal position. We can use rollouts there, moving the unresolved policy assumptions one level farther down. We can then expand our next responses, the opponent's responses after those, and so on, subject to the budget.

### 3.5. Formalize this as lookahead with a policy-based boundary value

Return to the usual convention: each state's value is measured for its own player to move. For the moment, suppose $V^{\pi_0}$ is known exactly so we can isolate the effect of lookahead from sampling noise. Define

$$
F_h(s)=
\begin{cases}
z(s), & s\text{ terminal},\\[3pt]
V^{\pi_0}(s), & h=0\text{ and }s\text{ nonterminal},\\[3pt]
\displaystyle\max_{a\in\mathcal A(s)}[-F_{h-1}(T(s,a))],
& h\ge1\text{ and }s\text{ nonterminal}.
\end{cases}
$$

The meanings are:

- $F_0(s)$: both sides follow the baseline immediately.
- $F_1(s)$: optimize the next move, then use the baseline.
- $F_2(s)$: optimize our move and the opponent's reply, then use the baseline.
- $F_h(s)$: solve the first $h$ plies adversarially, then use the baseline to value unresolved continuations.

The root decision with $h\ge1$ is

$$
a_h(s)\in\arg\max_a[-F_{h-1}(T(s,a))].
$$

In practice we can replace $V^{\pi_0}$ at the boundary with its rollout estimate, giving approximate values $\widehat F_h$. Terminal states still receive exact outcomes.

This supplies a concrete next step after the two-error decomposition: **use some computation to estimate boundary values and some computation to move the boundary deeper, explicitly examining decisions that the baseline might get wrong.**

Deeper search does not guarantee that every intermediate estimate is closer to $V^*$, or that every selected move improves. The boundary values can be misleading. But if the lookahead covers the entire finite remaining game tree, every boundary is terminal and $F_h(s)=V^*(s)$; the baseline no longer matters.

We can also turn this procedure into a continuation policy: whenever it is a player's turn, run the limited search and choose its recommended move. This defines a new policy $\pi_1$, which we can evaluate in turn. Its self-play value is not automatically equal to $F_h$: during actual play, both players search again at subsequent turns instead of permanently switching to $\pi_0$ at the original horizon. Improving both sides this way is a method to investigate, not a general theorem of monotonic self-play improvement.

The full-width recurrence above is a teaching model. Leela's MCTS, introduced in Section 4, allocates visits selectively and backs up averages; it does not literally compute this full-width maximum recurrence at every backup. The shared motivation is to investigate decisions explicitly instead of trusting a fixed continuation policy everywhere.

### 3.6. Replace the boundary evaluation with a learned estimate, then identify the further changes in Leela

Section 3.5 defines an algorithm with two ingredients: expand all legal choices for $h$ plies, and assign $V^{\pi_0}(u)$ to each unresolved position $u$ at that depth. We will first change only the second ingredient. That gives an exact mathematical continuation of Section 3.5. Afterward, we will identify the additional changes needed to reach Leela's algorithm.

#### 3.6.1. Specify exactly where a boundary value is used

Fix a root $s$ and a lookahead depth $h$. A **boundary position** $u$ is a nonterminal position reached after $h$ moves from that root, where the depth-limited procedure stops expanding. A branch ending earlier in a terminal state uses its known payoff instead.

For example, with $h=2$, the procedure explicitly considers our candidate moves and the opponent's replies. The nonterminal positions after those replies are the boundary. It needs a value for each of them to perform the maximizations above them. In Section 3.5, that value is $V^{\pi_0}(u)$.

The boundary belongs to a particular search, not to the game state intrinsically. Increasing $h$ may cause us to expand a position that was previously on the boundary. Also, evaluating a boundary position requires predicting an outcome from that position; the position itself is known exactly.

#### 3.6.2. Make the boundary evaluator an explicit argument

Let $e(u)$ be any function supplying a scalar evaluation for nonterminal positions, from the player-to-move perspective. Define the lookahead using that evaluator:

$$
G_h^e(s)=
\begin{cases}
z(s), & s\text{ terminal},\\[3pt]
e(s), & h=0\text{ and }s\text{ nonterminal},\\[3pt]
\displaystyle\max_{a\in\mathcal A(s)}[-G_{h-1}^e(T(s,a))],
& h\ge1\text{ and }s\text{ nonterminal}.
\end{cases}
$$

This merely exposes the evaluator that was implicit in $F_h$. In particular,

$$
\boxed{F_h(s)=G_h^{V^{\pi_0}}(s).}
$$

The superscript $e$ specifies the boundary evaluator; the subscript $h$ specifies how far we explicitly examine decisions before using it. They have different roles.

Evaluating $V^{\pi_0}(u)$ by completing games from every boundary position can be expensive. If each continuation has roughly $H$ remaining moves, each move under $\pi_0$ costs roughly $C$, and we run $n$ continuations, one boundary evaluation costs roughly $nHC$. A baseline that itself runs search can make $C$ large.

We therefore consider a function $v_\theta(u)$ that predicts $V^{\pi_0}(u)$ more cheaply. A neural network is one possible choice of function class. Substituting it gives

$$
\boxed{\widehat F_{h,\pi_0}(s):=G_h^{v_\theta}(s).}
$$

Here $\pi_0$ identifies the continuation policy whose lookahead value we aim to approximate: the target is the $F_h$ defined in Section 3.5, with $\pi_0$ held fixed. The right-hand side identifies the predictor used in the calculation, $v_\theta\approx V^{\pi_0}$. Dependence on the fitted parameters $\theta$ is implicit on the left. The hat describes the approximation to that target; it does not introduce another estimation step beyond using $v_\theta$.

The resulting action is

$$
\hat a_{h,\pi_0}(s)
\in\arg\max_a[-G_{h-1}^{v_\theta}(T(s,a))].
$$

**This substitution is the precise next step from 3.5: learn an approximation to $V^{\pi_0}$, use it at the boundary, and calculate the lookahead above it.** It does not require the network to predict $F_h(s)$ directly. If $v_\theta(u)=V^{\pi_0}(u)$ at every boundary position of this search, then $\widehat F_{h,\pi_0}(s)=F_h(s)$.

More generally, if every boundary prediction differs from $V^{\pi_0}$ by at most $\varepsilon$, then

$$
|\widehat F_{h,\pi_0}(s)-F_h(s)|\le\varepsilon.
$$

To see why, compare two calculations on the same depth-$h$ tree: one uses $V^{\pi_0}$ at the boundary, and the other uses $v_\theta$. Both use exact payoffs at terminal states.

At the nonterminal boundary, assume

$$
|v_\theta(u)-V^{\pi_0}(u)|\le\varepsilon
\qquad\text{for every boundary position }u.
$$

Now move up one level. A parent negates its children's values and takes their maximum. Negation preserves absolute error. Taking a maximum also cannot enlarge the largest error between corresponding entries:

$$
\left|\max_a x_a-\max_a y_a\right|
\le\max_a|x_a-y_a|.
$$

Why? If every $x_a$ is within $\varepsilon$ of $y_a$, then every $x_a$ is at most $\max_b y_b+\varepsilon$. Taking the maximum gives

$$
\max_a x_a-\max_a y_a\le\varepsilon.
$$

Interchanging the two collections gives

$$
\max_a y_a-\max_a x_a\le\varepsilon,
$$

or equivalently,

$$
\max_a x_a-\max_a y_a\ge-\varepsilon.
$$

Combining these inequalities yields

$$
-\varepsilon\le\max_a x_a-\max_a y_a\le\varepsilon,
$$

which is exactly

$$
\left|\max_a x_a-\max_a y_a\right|\le\varepsilon.
$$

The absolute-value bound requires both inequalities; the first alone would still allow an arbitrarily large negative difference. This argument holds even if different actions achieve the two maxima. We use $\le$ because the original error assumption allows equality.

Consequently, if all children have error at most $\varepsilon$, their parent does too. The boundary satisfies this condition by assumption, and terminal states have zero error. Repeating the same argument one level at a time reaches the root, giving

$$
\left|G_h^{v_\theta}(s)-G_h^{V^{\pi_0}}(s)\right|
=|\widehat F_{h,\pi_0}(s)-F_h(s)|
\le\varepsilon.
$$

This is the induction: the error bound holds at the boundary and survives each step upward. Errors do not add across levels because the recurrence uses negation and maximization. The result bounds the extra value error from substituting the network; it does not establish that $F_h$ is close to $V^*$ or that the chosen action remains the same. It also requires accuracy at **every** nonterminal boundary position; low average training error is insufficient.

#### 3.6.3. Explain how the proposed boundary predictor gets its target

For this fixed-policy construction, collect pairs $(u_j,Z_j)$ by recording a position $u_j$, following $\pi_0$ for both sides to the end, and expressing $Z_j$ from the perspective of the player to move at $u_j$. Fit $v_\theta$ to these outcomes with squared error.

At a given state $u$, the unrestricted population minimizer is

$$
\arg\min_{v\in\mathbb R}\mathbb E_{\pi_0}[(Z-v)^2\mid u]
=\mathbb E_{\pi_0}[Z\mid u]
=V^{\pi_0}(u).
$$

This establishes why those training outcomes are appropriate for the proposed replacement. A finite network trained on finite data only approximates that minimizer. The learned mapping can reuse experience across positions, so we need not generate fresh continuations every time search requests a boundary value.

Calling this a *boundary evaluator* describes how search uses the function. Training positions need not have been boundaries of a particular search; they only need outcome targets under the specified continuation policy. The same function can be called at different depths.

At this point we have derived **depth-limited minimax with a learned boundary evaluator**. We have not yet derived Leela's particular search algorithm.

### 3.7. Close the fixed-depth construction before choosing another search algorithm

We now have a complete search procedure: expand every legal continuation to depth $h$, use exact results at terminal positions and $v_\theta$ at nonterminal boundary positions, and back up by negation and maximization. Its computed value is

$$
\widehat F_{h,\pi_0}(s)=G_h^{v_\theta}(s).
$$

This procedure stands on its own. Introducing a neural evaluator does not require us to adopt MCTS, add a policy output, or train through self-play. Those are further choices, each requiring its own motivation.

**What has learning addressed?** It supplies a reusable approximation to the boundary value $V^{\pi_0}$, avoiding fresh continuations under $\pi_0$ whenever search needs a boundary evaluation. It has not removed the combinatorial cost of constructing the tree. Even if one evaluation were cheap, evaluating every position at a shared depth would still require a rapidly growing number of evaluations as $h$ increases. Nor does the learned evaluator remove the difference between the continuation value $V^{\pi_0}$ and optimal play.

The next question therefore concerns the search budget: **given a fixed evaluator, where should we spend additional computation to improve our root decision?** Increasing $h$ everywhere is one answer. Expanding selected boundary positions more deeply is another. The latter can retain exactly the same minimax backup; changing the shape of the tree does not by itself require changing how values are combined.

That is the direct continuation into Chapter 4. We first formulate selective minimax, so there is no unexplained change of algorithm at the chapter boundary. We then introduce MCTS as a different approach to selecting paths and combining their evaluations. Its visit counts, running averages, and exploration rule need separate definitions and justification; they are not consequences of replacing $V^{\pi_0}$ with $v_\theta$. We study this branch because it underlies the existing engine, without treating it as the only possible continuation of Section 3.

There are consequently three distinct questions ahead:

- **Search construction (Chapter 4):** how does the partial tree grow, and what do the stored estimates actually compute?
- **Search justification (Chapter 5):** how can adversarial selection together with average backups recover minimax values, and what does that establish about a finite search budget?
- **Learning from search (Chapter 6):** once a search procedure exists, how can it generate training targets for a value evaluator and a move prior?

Until the third question, hold the evaluator fixed. Its fixed-policy training target above explains one coherent way to obtain such a function; it does not claim that AlphaZero or a particular Leela network uses that same target. We can understand how search uses an evaluator before changing how the evaluator is trained.

## 4. From fixed-depth lookahead to selective search and MCTS

### 4.1. The remaining problem is where to spend the next unit of search

Keep the evaluator $v_\theta$ fixed. We already know how to combine its boundary predictions through a full tree of depth $h$. The new problem is to allocate a budget that may not permit the next complete level of that tree.

Our ultimate decision problem is still

$$
a^*\in\arg\max_{a\in\mathcal A(s)}Q^*(s,a),
\qquad Q^*(s,a)=-V^*(T(s,a)).
$$

Section 3 supplied one possible search procedure: expand every legal continuation to depth $h$, evaluate its boundary, and calculate $G_h^{v_\theta}$. Suppose we have finished that calculation and still have time left. **What should we investigate next?**

One answer is to increase $h$ by one. But that expands an entire new level, across all root actions and all their continuations. It may use much of the remaining budget on decisions whose current evidence is already sufficient for our purposes.

For example, suppose A currently looks much worse than B, while B and C look almost tied. Additional examination of B and C may change our choice. Another equally deep expansion under A may not. Conversely, if B's apparent strength depends on the opponent missing a particular defense, investigating that defense may be more useful than exploring another unrelated branch.

These are judgments based on incomplete evidence, not proofs that the neglected branches are irrelevant. We need a way to revisit alternatives, because current estimates can be wrong. Nevertheless, the decision problem suggests a different use of the budget: **choose the next position to expand according to how useful investigating it might be, rather than require every branch to reach the same depth.**

That is the motivation for selective search. It does not yet specify MCTS.

### 4.2. Go through selective minimax before introducing MCTS

#### 4.2.1. First change only the shape of the searched tree

We can keep the familiar boundary evaluator and minimax backup while allowing different branches to stop at different depths:

1. Begin with a shallow tree and evaluate its nonterminal boundary positions using $v_\theta$.
2. Choose one nonterminal boundary position for further investigation.
3. Expand it, evaluating its legal successor positions, using exact payoffs for any terminal successors.
4. Recalculate the values of its ancestors with the same negation-and-maximum rule as before.
5. Repeat until the budget ends.

For this construction, an expanded node includes all its legal children; unexpanded nonterminal nodes keep their boundary estimates. The resulting tree can have a deep branch beside a shallow branch. The minimax interpretation of its backed-up values remains the same: optimize the explicitly represented decisions and use the evaluator where expansion stops.

This is a direct continuation of Section 3. We have changed where we stop, while retaining the rule for combining boundary values. It is already a possible selective-search algorithm, provided we specify how to choose the next boundary position.

#### 4.2.2. Then identify the two choices MCTS makes

The preceding construction leaves a resource-allocation problem: which boundary position should receive the next expansion? A useful answer should account for both promising choices and alternatives whose limited investigation might have missed something important.

MCTS makes this a sequence of **local action-selection decisions**. Starting at the root, choose an action, then choose an action at its child, continuing until reaching a position to evaluate or expand. At each step, the player to move uses its own perspective. Section 5 gives the PUCT rule for balancing current values with further investigation.

To support those repeated decisions, MCTS stores two basic statistics for each action:

- How often search has investigated the action: a visit count $N$.
- What those investigations have returned: a running average $Q$ of backed-up leaf evaluations.

**Using a running average is an additional algorithmic choice.** Selective search does not mathematically require it. We could retain the selective minimax procedure above instead. MCTS uses counts and averages as evidence for allocating subsequent visits; its backup is not the minimax recurrence in another notation.

| Procedure | Where it expands | How it combines evaluations |
|---|---|---|
| Section 3's fixed-depth minimax | Every branch through depth $h$ | Negation and maximization |
| Selective minimax | Chosen boundary positions, at varying depths | Negation and maximization |
| MCTS described here | Paths chosen by repeated local selection | Running averages and visit counts, with adversarial selection at each node |

The first transition addresses uniform allocation of computation. The second chooses a particular way to gather evidence and use it to control that allocation. Neither is forced by the existence of a neural evaluator.

#### 4.2.3. Why can averages be useful in an adversarial search?

An average alone does not solve an adversarial game. Its usefulness depends on **which continuations generate the values being averaged**.

Recall the example where action A gives the opponent ten replies: nine lose for the opponent, while one wins for the opponent. Express every result from our perspective. If a fraction $\alpha_n$ of the first $n$ visits through A follows the opponent's winning defense, their average terminal result is

$$
Q_n(A)=(1-\alpha_n)(+1)+\alpha_n(-1)=1-2\alpha_n.
$$

Uniformly random replies give $\alpha_n\approx0.1$ and hence $Q_n(A)\approx0.8$, regardless of how accurately we measure that random policy. But an opponent choosing according to its own search evidence will favor its winning defense once discovered. **If** the visit fraction through that defense approaches one, then $Q_n(A)$ approaches $-1$, the correct minimax value in this example.

This calculation explains the intended mechanism: improve continuation choices at every player node, so later evaluations increasingly reflect strong opposition and responses. It does not prove that a particular finite search discovers the defense or allocates visits appropriately. The nine losing replies may look attractive until the critical reply is examined; exploration is essential. A selective minimax search that discovers all terminal replies can solve this tiny example directly as well. MCTS is an alternative allocation and accumulation procedure, not the only solution.

In larger trees, leaf values can also be predictions rather than terminal outcomes. This adds the evaluator's error to the problem. We therefore need to define the resulting average precisely, rather than claim it is automatically an unbiased estimate of $Q^*$.

#### 4.2.4. Put the resulting MCTS iteration together

We also allow each position to supply a distribution $P(s,\cdot)$ over its legal actions, called a *move prior*. It expresses which moves to investigate preferentially before much search evidence is available. It can be uniform; when a network supplies it, we write $P(s,a)=p_\theta(a\mid s)$ after restricting and normalizing over legal actions. This is an additional input to the selection rule, separate from the value prediction $v_\theta(s)$. Section 5 explains its use, and Chapter 6 explains how it can be learned.

An iteration now has four conceptual operations:

1. **Select:** follow a path using current values, visit counts, priors, and an exploration rule, with each player choosing from its own perspective.
2. **Expand:** when a previously unevaluated position is reached, determine its legal actions and whether it is terminal.
3. **Evaluate:** use the known terminal result, or obtain a prediction $v_\theta$ for a nonterminal leaf and policy priors for its actions.
4. **Back up:** convert that evaluation to the appropriate perspective, then update counts and running averages along the selected path.

A later iteration can continue through a previously evaluated position and investigate farther. Each iteration adds evidence that can change later path selections.

We will call the stored action-value estimate $\widehat Q_B(s,a)$ after budget $B$. Its intended use is to help identify actions with high $Q^*(s,a)$. What its formula actually computes is the average of evaluations gathered through that action by this changing search process. The next subsection defines that quantity exactly.

### 4.3. Define exactly which action-value estimate we construct

Fix an explored edge $(s,a)$. Let $N_B(s,a)$ be the number of completed visits through that edge after budget $B$. Index those visits by $j=1,\ldots,N_B(s,a)$.

On visit $j$, suppose the search travels $m_j$ plies from $s$, including action $a$, and stops at leaf $u_j$. Let

$$
\ell_j=
\begin{cases}
z(u_j), & u_j\text{ terminal},\\
v_\theta(u_j), & u_j\text{ nonterminal}.
\end{cases}
$$

This leaf value is measured for the player to move at $u_j$. Express it for the player who chose $a$ at $s$:

$$
Y_j(s,a)=(-1)^{m_j}\ell_j.
$$

The action-value estimate is

$$
\boxed{
\widehat Q_B(s,a)
=\frac{1}{N_B(s,a)}
\sum_{j=1}^{N_B(s,a)}Y_j(s,a)
}
\qquad\text{when }N_B(s,a)>0.
$$

This answers which estimator we are constructing: **for each action, an empirical average of backed-up leaf evaluations from visits that passed through that action**. The selected leaves and depths can change between visits, even with a frozen network.

For example, three visits through the same action might supply parent-perspective values $0.8$, $0.2$, and $-0.6$, giving $\widehat Q_B=0.4/3\approx0.133$. That is exactly the average of the evidence gathered so far. Calling it an action-value estimate does not make it equal to the true optimal-play value of the action.

The action's visit count also matters: a value based on one visit and the same value based on many visits represent different search histories. This motivates storing counts alongside values and using both in selection.

### 4.4. Store the estimate and update it along a path

For compactness, below and in Section 5 we write $N(s,a)$ and $Q(s,a)$ for the current $N_B(s,a)$ and $\widehat Q_B(s,a)$. **This stored $Q$ is distinct from the exact $Q^*$ and the fixed-policy $Q^{\pi_0}$.** Associate each explored edge with

$$
N(s,a)=\text{number of completed visits},
$$

$$
W(s,a)=\text{sum of backed-up evaluations},
$$

$$
Q(s,a)=\frac{W(s,a)}{N(s,a)}
\qquad\text{when }N(s,a)>0.
$$

Thus $W(s,a)=\sum_jY_j(s,a)$. Every evaluation entering it is expressed from the perspective of the player choosing $a$ at $s$.

To see how one leaf evaluation updates several edges, write the selected path as

$$
s_0\xrightarrow{a_0}s_1
\xrightarrow{a_1}\cdots
\xrightarrow{a_{L-1}}s_L.
$$

At $s_L$, obtain a value $v_L$: either an exact terminal payoff or an evaluator's estimate.

For strict alternating play, the value backed up to edge $(s_i,a_i)$ is

$$
g_i=(-1)^{L-i}v_L.
$$

Then update

$$
N(s_i,a_i)\leftarrow N(s_i,a_i)+1,
\qquad
W(s_i,a_i)\leftarrow W(s_i,a_i)+g_i.
$$

Equivalently, the average can be updated incrementally:

$$
Q_{\mathrm{new}}
=Q_{\mathrm{old}}
+\frac{g_i-Q_{\mathrm{old}}}{N_{\mathrm{old}}+1}.
$$

For example, suppose White moves, Black replies, and the resulting leaf is evaluated as $+0.6$ for White:

| Edge | Choosing player | Backed-up value |
|---|---|---:|
| White's move | White | $+0.6$ |
| Black's reply | Black | $-0.6$ |

The same evidence encourages White's move and discourages Black's reply. This is why perspective changes are essential: each player selects actions according to its own outcomes.

### 4.5. State what this estimate does and does not establish

In the fixed-policy rollout estimator, each continuation follows the same specified policy and its complete outcome has expectation $Q^{\pi_0}(s,a)$. Here the situation has changed in two ways:

- **The continuation choices change:** as visits and values change, later paths can follow different decisions from earlier ones. The same action can lead to evaluations under different continuation behavior over the course of a search.
- **The evaluations can be truncated:** a nonterminal leaf contributes a network prediction rather than a completed outcome. Even if that predictor were exact for $V^{\pi_0}$, the explicit choices made before reaching it need not follow $\pi_0$.

Therefore the usual argument “a sample mean estimates the expectation under one fixed policy” cannot simply identify this quantity with $Q^{\pi_0}$ or $Q^*$. Nor is it $G_h^{v_\theta}$: paths have different depths, and the backup stores an average instead of maximizing child values at every internal node. The uniform error bound in Section 3 applies to its specified full-width recurrence; it does not directly establish an analogous result for adaptive MCTS.

The motivation is nevertheless concrete. Additional visits can discover an opponent's strong defense or our own useful response, and those discoveries influence subsequent selections for both players. A misleading first leaf evaluation is no longer the only evidence available about an action. But finite search can still overlook a decisive line, and the statistics do not become proof-quality merely by receiving many visits.

The root action is selected from the search results when the budget ends. Its rule is another choice: it need not simply maximize $Q$, and the basic AlphaZero-style rule uses root visits. Section 6.1 explains that decision rule. The immediate next question is **which path to investigate on the next iteration**, because the quality of our accumulated evidence depends on that allocation. Section 5 introduces PUCT to make this choice.

This conceptual iteration follows the search structure described in [Lc0's AlphaZero primer](https://lczero.org/dev/lc0/search/alphazero/). The equations here use edge values in the choosing player's perspective; code may store equivalent statistics at child nodes and adjust signs accordingly.

### 4.6. What must selection accomplish for averages to recover minimax?

We have defined the stored averages, but have not yet justified their relationship to optimal play. This is the question the selection rule must help us answer.

An average backup does not perform a minimax operation. Consider a player choosing between two terminal outcomes, $+1$ and $-1$, both measured for that player. Its optimal value is $+1$. Investigating the two actions equally often gives a node average of zero. **This counterexample establishes that visiting every action is not sufficient: how visits are distributed matters.**

Fix a node $s$. In this subsection we suppress $s$ in the notation:

- $Q_t(a)$ means $Q_t(s,a)$: the stored average of leaf returns from visits through action $a$, after $t$ completed action visits at $s$. Only $N_t(s,a)$ of those visits went through $a$; its average uses those visits, not all $t$. This is a changing search statistic, not a value defined by optimal play.
- $q_a$ denotes a hypothetical limiting value: we temporarily assume $Q_t(a)\to q_a$. At this point, we have not proved that the limit exists or that it is correct.
- $Q^*(s,a)=-V^*(T(s,a))$ is the value of choosing $a$ and then having both players play optimally. This is the optimal-play quantity we ultimately want the stored average to approach.

The argument below first establishes a conditional fact: **if** the action averages converge to some limits $q_a$, concentrating visits on the actions with largest limits makes the node average approach $\max_a q_a$. It does not yet identify those limits with $Q^*(s,a)$. Chapter 5 supplies that identification under its finite-tree assumptions.

Let $w_t(a)=N_t(a)/t$ be the fraction of the node's first $t$ action visits allocated to $a$. Its average of returns through its children is

$$
\sum_a w_t(a)Q_t(a),\qquad \sum_a w_t(a)=1.
$$

If $Q_t(a)\to q_a$, replacing those estimates by their limits changes this expression by at most $\max_a|Q_t(a)-q_a|$, which tends to zero. Now write $q^*=\max_a q_a$. Here $q^*$ is just the largest hypothetical limit; the notation does not assert that it equals $V^*(s)$. Once we prove $q_a=Q^*(s,a)$ for every action, that equality will follow. The difference between the maximum and the weighted average of the limits is

$$
q^*-\sum_a w_t(a)q_a
=\sum_{a:\,q_a<q^*}w_t(a)(q^*-q_a).
$$

For a finite set of actions, this tends to zero if every suboptimal action's visit fraction tends to zero. The average can therefore approach a maximum **because its weights concentrate on maximizing actions**, even though no maximization occurs in the backup itself.

This suggests two requirements for a simple selection method:

1. **Keep alternatives discoverable.** A mistaken early estimate must not permanently prevent an action from receiving more visits. We will ensure every action is visited infinitely often if its node is visited infinitely often.
2. **Concentrate effort as evidence settles.** Once action estimates converge, actions with inferior limiting values should receive a vanishing fraction of the visits.

These requirements are compatible: a count can grow without bound while its fraction of total visits tends to zero. They are sufficient properties for the argument we will develop, rather than requirements imposed on every possible search algorithm. A method with valid pruning proofs, for instance, can safely stop exploring some actions.

There is still a dependency to resolve: we have temporarily assumed that the action estimates converge. Chapter 5 will establish that fact upward from terminal nodes in a finite tree. At each node, its own player's selection concentrates on its own best actions; changing perspective by negation then supplies the adversarial relationship to its parent.

That is the route we will investigate. It motivates examining MCTS's selection and average backup together. It does not establish that MCTS is preferable to selective minimax, which remains a different valid construction.

## 5. Construct a selection rule, then establish what it guarantees

The immediate problem is now precise: choose actions so that early mistakes can be corrected, while increasingly concentrating work on the best continuations. We will first construct a basic rule meeting those qualitative requirements, then prove its consequence for a finite tree. This is a logical motivation for the algorithm, not a claim that its particular formula is uniquely necessary or historically inevitable.

### 5.1. Why greedy selection and uniform selection each miss a requirement

Greedy selection always chooses the largest current Q. If an action receives a misleadingly low initial evaluation, it can stop being selected. Its estimate then has no opportunity to improve. Greedy selection alone does not ensure our first requirement.

Uniform selection keeps investigating every action, but continues spending the same fraction on actions already established to be inferior. As the counterexample in Section 4.6 showed, averaging those continuations need not approach the player's optimal value. Uniform selection alone does not ensure our second requirement for average backups.

We therefore consider selection scores of the form

$$
S_t(a)=Q_t(a)+b_t(a),
$$

where $b_t(a)\ge0$ provides an additional incentive to investigate an action. Here t counts completed action visits at the current node; it is not necessarily the global number of root visits.

Two useful checks on a proposed bonus are:

- If an action's count n stays fixed while t grows, its bonus should grow, so neglect eventually becomes difficult to sustain.
- If its count is a fixed positive fraction of t, its bonus should tend to zero, so a persistent value disadvantage can eventually dominate.

These checks motivate a form; for an arbitrary bonus they are not by themselves a complete proof of the desired allocation. We will verify the properties directly for the particular rule we choose.

### 5.2. Introduce basic PUCT as one explicit design

One way to satisfy those scaling checks is to use $\sqrt t/(1+n)$. It grows when n is fixed, but when $n\approx\alpha t$ with $\alpha>0$, it decays like $1/(\alpha\sqrt t)$.

We can also use prior information to vary the incentive across actions. Let $P(s,a)$ be a fixed prior distribution at the node. It may be uniform, or supplied by the network's policy output. A basic PUCT rule is

$$
\boxed{
S_t(s,a)=Q_t(s,a)
+cP(s,a)\frac{\sqrt{\max\{t,1\}}}{1+N_t(s,a)},
\qquad t=\sum_bN_t(s,b),\quad c>0.
}
$$

Select an action maximizing this score, with a specified tie rule. For now, give an unvisited action provisional value zero; after its first completed visit, use its empirical average. This completes a simple algorithm rather than leaving the zero-count cases implicit. The floor in the numerator makes the first bonus $cP(s,a)$ rather than zero.

Each part now answers a preceding need. Q uses the evidence already gathered. The count denominator reduces the incentive for repeatedly investigated actions. The growing numerator prevents a permanently neglected positive-prior action from becoming irrelevant forever. P directs more exploration pressure toward actions considered promising before their search values are well established. The coefficient c controls the bonus relative to value differences.

The particular powers and linear use of P are **design choices**. Our requirements do not uniquely derive them, and the formula is not being presented as a calibrated confidence interval. The optional theory chapter gives a separate regularization interpretation and compares it with concentration-based UCB rules.

For the proof below, require $P(s,a)>0$ for every legal action. A zero prior supplies no exploration pressure through this bonus. A very small positive prior can still cause a long delay at finite budgets. Thus a prior is useful guidance only insofar as it helps allocate work in the setting of interest; it is not proof that low-prior actions are unimportant.

Leela's actual selection includes further choices, such as first-play urgency, an exploration coefficient that can change with visits, and accounting for pending evaluations. Chapter 7 introduces those in response to their own problems. The basic rule here is the mathematical model we will analyze.

### 5.3. Prove that this combination can recover minimax

We can now return to the question from Section 4.6. The proof is deliberately limited, so we can identify which mechanism establishes which result.

#### The algorithm and assumptions

Assume a deterministic, strictly alternating, zero-sum game with a finite tree and terminal payoffs in $[-1,1]$. Store each tree-node occurrence separately. Do not prune, merge transpositions, or evict nodes.

On the first visit to a nonterminal node, create all legal action edges, obtain one fixed bounded prediction $e(s)\in[-1,1]$, and return that prediction. On later visits, select an action using the rule in Section 5.2 and continue recursively. Terminal nodes always return their exact payoff. Each visit backs up its returned evaluation through its path using the signs and average updates in Chapter 4.

All priors are fixed and strictly positive, c is fixed and positive, and the root receives indefinitely many visits. There is no permanent nonterminal depth cutoff: later visits can always investigate farther. The network predictions may be wrong; they enter once when each nonterminal node is first evaluated.

**Claim:** edge averages converge to $Q^*$, node averages converge to $V^*$, and a most-visited root action is eventually optimal. We will prove this through two local selection facts and induction on the finite tree.

#### First fact: no positive-prior action is permanently ignored

At a node receiving infinitely many visits, all Q values remain bounded. Suppose some action were visited only finitely often. Its integer count would eventually become constant.

For $t\ge1$, divide selection scores by $\sqrt t$:

$$
\frac{S_t(s,a)}{\sqrt t}
=\frac{Q_t(s,a)}{\sqrt t}
+\frac{cP(s,a)}{1+N_t(s,a)}.
$$

For an infinitely visited action, both terms tend to zero. For a finitely visited action, the first tends to zero but the second tends to a positive constant. Because there are finitely many actions, eventually the selected action would have to come from the supposedly finitely visited set. Continually selecting from that set contradicts its finite total number of visits.

Consequently every action is visited infinitely often. The root's children therefore receive infinitely many visits, and the same argument applies recursively to every descendant.

#### A convergence fact we will use

We need the definition of convergence of a real sequence. Let $\mathbb N=\{1,2,3,\ldots\}$ and let $x:\mathbb N\to\mathbb R$ be a sequence, with terms $x_t=x(t)$. For a proposed limit $L\in\mathbb R$, the formal definition is

$$
\boxed{
\lim_{t\to\infty}x_t=L
\quad\Longleftrightarrow\quad
(\forall\varepsilon\in\mathbb R_{>0})
(\exists T\in\mathbb N)
(\forall t\in\mathbb N)
\bigl[t\ge T\implies |x_t-L|<\varepsilon\bigr].
}
$$

Here $\mathbb R_{>0}$ is the set of positive real numbers, $\forall$ means “for every,” $\exists$ means “there exists,” $\in$ means “belongs to,” $\implies$ means “implies,” and $\Longleftrightarrow$ means “if and only if.” We also write $x_t\to L$.

In words: however small a positive distance you choose, eventually every remaining term stays within that distance of $L$. Read the quantifiers in order:

1. Choose any positive tolerance $\varepsilon$.
2. There must exist an integer threshold $T$, which may depend on that tolerance.
3. With that one threshold fixed, every $t\ge T$ must satisfy $|x_t-L|<\varepsilon$.

Thus $T$ cannot be chosen separately for each later $t$: it must work for the entire remaining sequence. Every integer $T$ is finite; $T=\infty$ is not allowed. Convergence guarantees that a threshold exists, without necessarily telling us how large it is. The terms need not move monotonically toward $L$ or ever equal it.

For example, $x_t=1/t$ converges to zero: given any $\varepsilon>0$, choose an integer $T>1/\varepsilon$. Then every $t\ge T$ satisfies $|1/t-0|\le1/T<\varepsilon$. By contrast, $0,1,0,1,\ldots$ does not converge to zero: it repeatedly leaves the interval of radius $1/2$ around zero, however far along the sequence we go.

To apply this definition below, suppose $Q_t(s,a^*)\to q_{a^*}$ and $Q_t(s,a)\to q_a$, with positive limiting gap $\Delta_a=q_{a^*}-q_a$. Choose $\varepsilon=\Delta_a/4$. Convergence gives:

- A finite threshold $T_1$ after which $|Q_t(s,a^*)-q_{a^*}|<\Delta_a/4$.
- A finite threshold $T_2$ after which $|Q_t(s,a)-q_a|<\Delta_a/4$.

Taking $T=\max\{T_1,T_2\}$ makes both bounds hold for every $t\ge T$. Each estimate can shrink their apparent separation by less than a quarter of the limiting gap. Together they therefore leave at least

$$
\Delta_a-\frac{\Delta_a}{4}-\frac{\Delta_a}{4}
=\frac{\Delta_a}{2}
$$

of that gap. The existence of the thresholds comes from convergence; the quarter-gap tolerance is our choice, made to obtain the half-gap bound. Fact 2 spells out the resulting inequality and how selection uses it.

#### Second fact: if action estimates converge, inferior actions get a vanishing fraction of visits

Fix one node $s$ that receives infinitely many visits. Here $t$ counts completed action visits at this node, and $Q_t(s,a)$ and $N_t(s,a)$ are its statistics **before** selecting the next action. Temporarily assume that, for every legal action,

$$
Q_t(s,a)\longrightarrow q_a.
$$

These are hypothetical limits, not yet identified with $Q^*(s,a)$. Choose one action $a^*$ attaining the largest limit, and write

$$
q^*=q_{a^*}=\max_a q_a.
$$

In this fact, $a^*$ means an action maximizing the limiting values. We will only identify it with an optimal game action after the induction below.

**First obtain a finite-time separation between the estimates.** Fix an inferior action $a$, with limiting gap

$$
\Delta_a=q_{a^*}-q_a>0.
$$

Convergence means that there is a finite integer $T\ge1$ such that, for every $t\ge T$,

$$
|Q_t(s,a^*)-q_{a^*}|\le\frac{\Delta_a}{4},
\qquad
|Q_t(s,a)-q_a|\le\frac{\Delta_a}{4}.
$$

Consequently, at every such time,

$$
\begin{aligned}
Q_t(s,a^*)-Q_t(s,a)
&\ge \left(q_{a^*}-\frac{\Delta_a}{4}\right)
     -\left(q_a+\frac{\Delta_a}{4}\right)\\
&=\Delta_a-\frac{\Delta_a}{2}
=\frac{\Delta_a}{2}.
\end{aligned}
$$

The estimated difference converges to the full gap $\Delta_a$. We use half the gap because we need a positive lower bound valid at every sufficiently late **finite** time. The difference might approach $\Delta_a$ from below without ever reaching it. Any fixed fraction strictly between zero and one could replace one half, with corresponding changes to the constants.

**Next ask when PUCT can still select the inferior action.** Write

$$
b_t(s,b)=\frac{cP(s,b)\sqrt t}{1+N_t(s,b)}
$$

for the bonus of any action $b$ at $t\ge1$. If PUCT selects $a$ at a time $t\ge T$, its score must be at least the score of $a^*$, even if the scores tie:

$$
Q_t(s,a)+b_t(s,a)
\ge Q_t(s,a^*)+b_t(s,a^*).
$$

Rearranging, and using the nonnegativity of the other action's bonus, gives

$$
\begin{aligned}
b_t(s,a)
&\ge Q_t(s,a^*)-Q_t(s,a)+b_t(s,a^*)\\
&\ge Q_t(s,a^*)-Q_t(s,a)\\
&\ge\frac{\Delta_a}{2}.
\end{aligned}
$$

Thus its bonus must overcome at least the half-gap. Substituting the bonus formula yields the following **necessary condition for selecting $a$**:

$$
\frac{cP(s,a)\sqrt t}{1+N_t(s,a)}\ge\frac{\Delta_a}{2},
\qquad
1+N_t(s,a)\le C_a\sqrt t,
\quad C_a:=\frac{2cP(s,a)}{\Delta_a}.
$$

This inequality has so far been proved only at times when $a$ is selected. We must explain why it also bounds the count at other times.

**Convert the selection condition into a bound for every later time.** For any integer $t\ge T$, there are two possibilities:

- If $a$ has not been selected since time $T$, then $N_t(s,a)=N_T(s,a)$.
- Otherwise, let $u$ be its last selection time with $T\le u<t$. That selection increased its count by one, and no later selection changed it. Therefore
  $$
  N_t(s,a)=N_u(s,a)+1\le C_a\sqrt u\le C_a\sqrt t.
  $$

In either case,

$$
N_t(s,a)\le\max\{N_T(s,a),\,C_a\sqrt t\}.
$$

Dividing by $t$ now proves the desired conclusion:

$$
0\le\frac{N_t(s,a)}{t}
\le\max\left\{\frac{N_T(s,a)}{t},\,\frac{C_a}{\sqrt t}\right\}
\longrightarrow0.
$$

The first fact says that the action still receives infinitely many visits. The second says that its fraction of all visits vanishes, **provided the action estimates converge and this action's limit is inferior**. Neither fact alone has established that the limiting values are correct. We establish that next.

#### Induction: show that the limiting values are the minimax values

The first fact already ensures that every node in the finite tree receives infinitely many visits. We can therefore discuss the limit of its returned-value average as its own visit count grows.

Let $M(s)$ denote the current average of all evaluations returned by visits to $s$, expressed for the player to move there. We suppress its visit index here; every limit below is taken as that node receives more visits. At a terminal node, every return equals its exact payoff $z(s)$, so

$$
M(s)=z(s)=V^*(s).
$$

This is the base case. For the induction step, take a nonterminal node $s$ and suppose that every child's average converges to that child's optimal value:

$$
M(T(s,a))\longrightarrow V^*(T(s,a))
\qquad\text{for every legal }a.
$$

Since this model stores each tree-node occurrence separately, the visits through edge $(s,a)$ are exactly the visits to its child. Their returned values have opposite signs, because the player to move changes. Hence, once the edge has been visited,

$$
Q_t(s,a)=-M(T(s,a)).
$$

As $t$ grows, the child's visit count also grows without bound by the first fact. The induction hypothesis therefore gives

$$
Q_t(s,a)\longrightarrow -V^*(T(s,a))=Q^*(s,a).
$$

**This is where we identify the hypothetical limits in the second fact:** at this node, $q_a=Q^*(s,a)$, and consequently

$$
q^*=\max_aQ^*(s,a)=V^*(s).
$$

The second fact now applies: every action with $Q^*(s,a)<V^*(s)$ receives a vanishing fraction of visits.

It remains to prove that the node's own average also converges to $V^*(s)$. After its one initial prediction $e(s)$ and $t$ subsequent visits through children, its average is exactly

$$
M(s)=\frac{e(s)+\sum_aN_t(s,a)Q_t(s,a)}{1+t}.
$$

For $t>0$, put $w_t(a)=N_t(s,a)/t$ and define the average over child visits as

$$
A_t=\sum_a w_t(a)Q_t(s,a).
$$

To bound the error between $A_t$ and $V^*(s)$, introduce an intermediate quantity:

$$
B_t=\sum_a w_t(a)Q^*(s,a).
$$

This uses the **same visit weights** as $A_t$, but replaces the stored action estimates with their exact optimal-play values. It is a quantity used in the proof, not something the search can compute. Comparing $A_t$ with $B_t$ isolates estimation error; comparing $B_t$ with $V^*(s)$ isolates the effect of allocating visits to inferior actions.

Insert $B_t$ and apply the triangle inequality:

$$
A_t-V^*(s)=(A_t-B_t)+(B_t-V^*(s)),
$$

$$
|A_t-V^*(s)|\le |A_t-B_t|+|B_t-V^*(s)|.
$$

**First bound: inaccurate action estimates.** Since the weights are nonnegative and sum to one,

$$
\begin{aligned}
|A_t-B_t|
&=\left|\sum_a w_t(a)\bigl(Q_t(s,a)-Q^*(s,a)\bigr)\right|\\
&\le\sum_a w_t(a)|Q_t(s,a)-Q^*(s,a)|\\
&\le\sum_a w_t(a)\max_b|Q_t(s,b)-Q^*(s,b)|\\
&=\max_b|Q_t(s,b)-Q^*(s,b)|.
\end{aligned}
$$

The first inequality is the triangle inequality for a finite sum. The second replaces each absolute error by the largest absolute error over all actions; nonnegative weights preserve that inequality. The last equality uses $\sum_a w_t(a)=1$. Thus a weighted average of errors cannot exceed the largest error. The letter $b$ is just an action index used to take that maximum independently of the outer sum over $a$.

**Second bound: visits allocated to inferior actions.** By the minimax definition, $Q^*(s,a)\le V^*(s)$ for every legal action. Therefore

$$
B_t=\sum_a w_t(a)Q^*(s,a)
\le\sum_a w_t(a)V^*(s)=V^*(s).
$$

We can consequently remove the absolute value with a known sign:

$$
\begin{aligned}
|B_t-V^*(s)|
&=V^*(s)-B_t\\
&=\sum_a w_t(a)V^*(s)-\sum_a w_t(a)Q^*(s,a)\\
&=\sum_a w_t(a)\bigl(V^*(s)-Q^*(s,a)\bigr)\\
&=\sum_{a:\,Q^*(s,a)<V^*(s)}w_t(a)\bigl(V^*(s)-Q^*(s,a)\bigr).
\end{aligned}
$$

In the second line we again used that the weights sum to one. In the last line we removed optimal actions because their gap $V^*(s)-Q^*(s,a)$ is zero. This expression is an exact equality: the loss from averaging the true action values is the sum of each inferior action's visit weight times its optimality gap.

Combining the two results gives

$$
\begin{aligned}
|A_t-V^*(s)|
&\le \max_a|Q_t(s,a)-Q^*(s,a)|
  +\sum_{a:\,Q^*(s,a)<V^*(s)}w_t(a)\bigl(V^*(s)-Q^*(s,a)\bigr).
\end{aligned}
$$

This bound is algebraic: it holds at any time when the action averages are defined, even if those averages are inaccurate. To conclude convergence, we now use the facts already established in the induction:

- The first term tends to zero because every $Q_t(s,a)$ converges to $Q^*(s,a)$. There are finitely many actions, so, for any positive tolerance, we can take the largest of their individual thresholds; after that time every error, and hence their maximum, is below the tolerance.
- The second term tends to zero because Fact 2 gives $w_t(a)\to0$ for each inferior action. Each gap is a fixed finite number, so each product tends to zero. There are finitely many such products, so their sum also tends to zero.

The nonnegative error $|A_t-V^*(s)|$ is bounded above by a quantity tending to zero. Therefore $A_t\to V^*(s)$.

The important mechanism is that the average need not explicitly take a maximum: its weights increasingly concentrate on actions whose limiting values attain that maximum. This conclusion uses both accurate limiting estimates and vanishing weights on inferior actions. The inequality alone guarantees neither property and gives no rate at which the search obtains them.

Finally,

$$
M(s)=\frac{e(s)}{1+t}+\frac{t}{1+t}A_t
\longrightarrow V^*(s).
$$

The initial prediction's weight vanishes, while the average over subsequent child visits approaches the correct value. This completes the induction step. Working upward from the terminal nodes proves convergence of node averages to $V^*$ and edge averages to $Q^*$ throughout the finite tree. We have not assumed convergence of the whole search in advance: correctness at the children establishes convergence of the action estimates at their parent, which lets us apply the second fact there.

**Finally justify the most-visited root recommendation.** At the root, let $K\ge1$ be the number of optimal actions and let $R_t$ be the total number of visits to suboptimal actions. Since there are finitely many suboptimal actions and each has a vanishing visit fraction,

$$
\frac{R_t}{t}\longrightarrow0.
$$

The optimal actions together have $t-R_t$ visits. At least one therefore has count at least $(t-R_t)/K$, whereas every suboptimal action has count at most $R_t$. For all sufficiently large $t$,

$$
\frac{t-R_t}{K}>R_t,
$$

because $R_t/t\to0$. Thus the largest optimal-action count eventually exceeds every suboptimal-action count. Any most-visited root action is then optimal; if every root action is optimal, this conclusion holds immediately.

The mechanism is now explicit: **selection makes the weights of an average concentrate on maximizing children; negation changes perspective between players; induction connects those local limits through the game tree.** The average backup itself has never become a minimax operation.

### 5.4. Why this result leaves finite-budget performance unresolved

The proof answers whether the specified algorithm can eventually recover minimax. It gives no useful general bound on how soon the child estimates enter the accurate regime assumed in the second fact. A root action may depend on discovering a rare defense several levels down, whose own value depends on another difficult discovery.

The finite-tree assumption also means the algorithm eventually explores the entire finite tree. Neural predictions are used only once per new node and their influence is diluted by later visits. That is enough for the proof, but solving chess by eventual exhaustive coverage is exactly what motivated our budget constraint. The theorem must not be mistaken for a computational-efficiency argument.

Finite-budget performance instead depends on whether the prior directs early visits toward relevant actions and whether leaf predictions provide useful information before a subtree is solved. These can make partial exploration productive. They can also fail: a small prior on the only saving move or a confident error at a crucial leaf can delay the right conclusion. Neither monotonic improvement with every visit nor superiority over selective minimax follows from the proof.

Exact discoveries expose another limitation. If an opponent has a terminal winning reply, minimax can immediately use that fact. A running average may remain influenced by earlier favorable returns. This counterexample motivates adding solved-outcome propagation, which we will develop in Section 7.1.

The published UCT analysis establishes results for its own planning assumptions; it does not automatically cover arbitrary neural PUCT implementations. The proof above is self-contained for the explicit basic model. [Kocsis and Szepesvari, original UCT analysis](https://aima.cs.berkeley.edu/~russell/classes/cs294/s11/readings/Kocsis%2BSzepesvari%3A2006.pdf)

Known hard tree environments show that UCT and related AlphaGo-style rules can require extremely many visits before discovering the right action. Such results limit blanket efficiency claims; they do not establish typical chess performance. [Orseau and Munos, 2024](https://arxiv.org/abs/2405.04407)

### 5.5. Where to study the remaining mathematical questions

The core argument now establishes a qualified connection to minimax. The next mathematical questions concern rates, final-action error, prior quality, and alternatives. The separate [PUCT theory chapter](puct-theory.md) distinguishes cumulative from simple regret, explains which UCB1 bounds apply in stationary bandits, develops a regularization interpretation of PUCT, and identifies the extra assumptions needed in tree search.

That branch can be studied later. We can next ask a different question arising from the finite-budget limitation: how can we improve the information supplied to the next search without repeating all of the previous search's work?

## 6. Why evaluating current play can help produce better play

Chapter 5 held the evaluator fixed and asked what increasingly long search could establish. We now ask a different question: can learning from completed searches and games make the next engine stronger at a limited search budget?

Predicting current play accurately is not sufficient by itself. A weak policy can be perfectly imitated, and its bad outcomes can be perfectly predicted. We therefore need to explain an additional operation that turns knowledge of current play into different decisions. We will first define the playing policy, then construct and prove an exact evaluation-and-improvement loop. Only afterward will we replace its ideal operations with sampled training and finite-budget MCTS.

### 6.1. First turn search statistics into an actual move

The PUCT score chooses the next hypothetical investigation. Playing the action with the largest exploration bonus after the budget ends would answer a different question: an action can deserve another investigation precisely because it is poorly understood.

One basic final rule is to play a most-visited root action. Section 5.3 proved that this rule eventually selects an optimal action in its limited model. At finite budget, it expresses a practical preference for the choice that accumulated the most search attention; attention is not itself a calibrated confidence measure. Choosing the largest Q, or using extra solved information, are other rules that require separate assessment.

Once a final rule and search settings are fixed, the engine defines a playing policy. Write

$$
\mu_\theta(\cdot\mid s)
=\mathcal S_B[p_\theta,v_\theta](\cdot\mid s),
$$

where $\mathcal S_B$ includes the search budget and the rule converting its results into a played move. A deterministic choice is a distribution assigning probability one to one action. For this teaching model, use a fresh tree at each call and fix all other settings. Any move-number-dependent settings belong in the state or specified policy.

Keep three functions separate:

| Function | Question it answers |
|---|---|
| $v_\theta(s)$ | What outcome do we predict from this position? |
| $p_\theta(a\mid s)$ | Which actions should search initially prioritize? |
| $\mu_\theta(a\mid s)$ | Which action will the engine actually play after search? |

The third can differ substantially from the second because the purpose of search is to investigate and sometimes revise the prior's preferences.

### 6.2. Construct an exact evaluation-and-improvement loop

For this construction, assume a finite, deterministic, strictly alternating, perfect-information, zero-sum game tree. A state includes all information needed to determine its successors and outcome. Terminal values use the player-to-move convention from earlier chapters. These assumptions let us use the same negation and minimax equations.

Let $\mu_k(a\mid s)$ describe both players' behavior in generation $k$: at any nonterminal state, the player to move uses that distribution. This is one policy over states for both sides, not one player choosing the opponent's moves. The initial $\mu_0$ can be arbitrary and must be defined at every nonterminal node, including nodes it never reaches from the initial position. The index $k$ counts policy updates, not search visits or moves in a game.

**Evaluate the current behavior.** Temporarily suppose we can obtain $V^{\mu_k}(u)$ exactly at every state $u$. This is the expected terminal outcome if both players follow the frozen policy $\mu_k$ from $u$ onward, expressed for the player to move at $u$. Exact evaluation is an assumption of this teaching construction, not a claim about what a finite training dataset supplies.

It lets us evaluate a deviation from the current policy:

$$
Q^{\mu_k}(s,a)=-V^{\mu_k}(T(s,a)).
$$

The question is: take $a$ now, then let both players follow $\mu_k$. We can ask this about any legal action, including one to which $\mu_k$ assigns probability zero. A prediction about the chosen move alone would not suffice; we need the continuation values of alternatives.

**Use that evaluation to change the decision.** Define a new policy supported on maximizing actions:

$$
\mu_{k+1}(a\mid s)>0
\quad\Longrightarrow\quad
a\in\arg\max_b Q^{\mu_k}(s,b).
$$

Choosing one maximizing action deterministically is sufficient. This is exact one-ply lookahead with the current policy value as its boundary evaluator. It is the construction from Section 3 with a boundary policy that we update between generations.

Why does this operation contain any improvement? The old policy averages over its available choices:

$$
V^{\mu_k}(s)=\sum_a\mu_k(a\mid s)Q^{\mu_k}(s,a).
$$

The new decision attains the maximum of those same values, so

$$
\sum_a\mu_{k+1}(a\mid s)Q^{\mu_k}(s,a)
=\max_a Q^{\mu_k}(s,a)
\ge V^{\mu_k}(s).
$$

This inequality compares **changing the current decision and then following the old policy** with following the old policy throughout. It does not yet compare $V^{\mu_{k+1}}$ with $V^{\mu_k}$, because under $\mu_{k+1}$ both players also change later decisions. We need an argument that accounts for those simultaneous changes.

The proposed loop is therefore: evaluate $\mu_k$ everywhere, construct the greedy policy $\mu_{k+1}$ everywhere using those evaluations, and repeat. Each update uses the previous generation's values; decisions are not secretly updated from leaves upward within one generation.

### 6.3. Prove why this idealized self-play loop reaches optimal play

Define the height $h(s)$ of a node as the maximum number of moves remaining on any path from it to a terminal node. Terminal nodes have height zero, and each child has strictly smaller height than its parent. The finite-tree assumption gives a finite maximum height $H$.

Call an action optimal at $s$ if it attains $\max_a Q^*(s,a)$. We will prove:

$$
\mathcal P(k):\quad
\mu_k\text{ selects only optimal actions at every nonterminal node of height at most }k.
$$

This is a statement about actions throughout those subtrees, not just about the result of one self-play game. In particular, if a policy selects only optimal actions throughout a subtree, its expected outcome there equals $V^*$: terminal outcomes supply the base, and at each parent every selected action has the minimax value.

**Base case.** $\mathcal P(0)$ places no restriction on $\mu_0$, since height-zero nodes are terminal and have no decisions. Regardless of the policy, their values are already exact:

$$
V^{\mu_0}(u)=z(u)=V^*(u).
$$

**First update.** At height 1, all children are terminal. Thus $Q^{\mu_0}(s,a)=Q^*(s,a)$ for every action there, even if $\mu_0$ plays badly elsewhere. The greedy update chooses an optimal action. This establishes $\mathcal P(1)$ without any initial expert policy.

**Induction step.** Assume $\mathcal P(k)$, and take any nonterminal node $s$ with $h(s)\le k+1$. Every child $u=T(s,a)$ has height at most $k$. Every decision farther down that child's subtree also has height at most $k$. By the induction hypothesis, $\mu_k$ plays optimally throughout that subtree. Hence

$$
V^{\mu_k}(T(s,a))=V^*(T(s,a))
\qquad\text{for every legal }a.
$$

Negating gives

$$
Q^{\mu_k}(s,a)=Q^*(s,a).
$$

The next greedy update therefore selects only optimal actions at $s$. Since this holds for every node of height at most $k+1$, it proves $\mathcal P(k+1)$. It also preserves optimal decisions at all previously established heights, even if tie-breaking changes which optimal action is selected.

After at most $H$ updates, the policy selects optimal actions everywhere. The proof uses both players' updates and does not freeze the opponent. Its mechanism is that correct decisions in later positions make the next generation's continuation values correct, allowing earlier decisions to become correct too.

This answers the question about the targets. Initially, $V^{\mu_0}$ may describe poor play. Once a defense is learned, subsequent outcomes reflect that defense. Evaluating the new policy changes earlier positions' values, and the next decision update can reject moves that depended on the old mistake. The training target changes as behavior changes, while terminal payoffs remain fixed by the game.

The theorem is about an exact operation over every state. It is not an efficient method for obtaining those values in a huge game tree, and it does not say actual neural training solves one extra ply per generation.

### 6.4. Identify what this proves, and what it does not

**Exact self-consistency rules out a suboptimal fixed point.** Suppose a policy $\mu$ selects only actions maximizing its own exact continuation values. Then, at every nonterminal state,

$$
\begin{aligned}
V^\mu(s)
&=\sum_a\mu(a\mid s)\bigl[-V^\mu(T(s,a))\bigr]\\
&=\max_a[-V^\mu(T(s,a))].
\end{aligned}
$$

Together with exact terminal values, this is the minimax recurrence. Induction on height gives $V^\mu=V^*$, and the policy's maximizing actions are consequently optimal. Mere imitation would allow any weak policy to persist. It is the greedy decision update, applied everywhere, that rules out such a fixed point. This observation characterizes fixed points; the preceding induction separately proves that our exact iteration reaches optimal play.

**Eventual optimality does not imply monotonic playing strength.** Consider a root where we can take an immediate draw or enter a position in which the opponent can choose between losing and winning. Suppose $\mu_0$ takes the draw at the root but would blunder and lose at the opponent's node. All outcomes in this example are measured from the root player's perspective.

The first greedy update changes our root action to entering that position: following the old opponent policy afterward would yield $+1$, better than the draw. The same update teaches the opponent to take its immediate win. Thus self-play under $\mu_1$ now gives us $-1$ at the root. Against an optimal opponent, our root policy has also worsened from drawing to losing. The second update evaluates the corrected defense and restores the root draw.

The example establishes a precise limit of the theorem: a policy update can exploit an opponent mistake that disappears in that very update. We proved eventual optimality and preservation of already optimal subtrees near the terminals, not improvement against every opponent after every generation. Self-play outcomes alone are also not a sufficient measure of strength: equally strong opponents can produce similar aggregate scores at very different levels of ability.

**There are now two different convergence arguments.**

| Argument | What changes? | What is assumed? | What is established? |
|---|---|---|---|
| Section 5.3 | Visit counts and averages within search | Fixed bounded evaluator; specified PUCT; unbounded visits on a finite tree | Search values approach minimax |
| Sections 6.2–6.3 | The playing policy between generations | Exact evaluation at every state; exact greedy updates on a finite tree | The policy eventually plays optimally everywhere |

Neither theorem alone proves convergence of a neural network trained on sampled data while finite-budget MCTS generates its decisions. They establish mechanisms that the practical system tries to approximate. We now examine that approximation explicitly.

### 6.5. Replace exact policy evaluation with outcome prediction

Freeze parameters $\theta_k$ and define the resulting playing policy $\mu_k$. Play complete games with both sides using it, running search again at each actual turn. Record the played positions $s_j$.

When a game ends, express its result as $z_j$ for the player to move at each recorded position. This supplies value-training pairs $(s_j,z_j)$. For the frozen policy,

$$
\mathbb E_{\mu_k}[Z\mid s]=V^{\mu_k}(s).
$$

By the squared-error argument from Section 3, the unrestricted population minimizer at s is therefore $V^{\mu_k}(s)$. Fitting $v_\theta$ to these labels teaches a fast prediction of the outcomes of games played using this search procedure.

This reuses the earlier regression framework with a new continuation policy. It does not directly teach $F_h$, which describes one fixed-depth lookahead followed by $\pi_0$. Nor does it provide exact $V^*$ labels: both self-play players can make mistakes.

The training game and an individual search visit have different roles. A hypothetical search path normally stops at a network evaluation. The actual game continues through many played moves until an outcome is available. That outcome can label earlier played positions. Hypothetical leaves never played in that game get no direct outcome label from it; predictions there rely on generalization from other experience. Labels from one game are correlated observations, not independent continuations from every recorded position.

The ideal theorem required $V^{\mu_k}$ at every state, including alternatives that current play avoids. The regression objective identifies that value only where training supplies suitable data. At an unobserved position, a small training loss places no direct constraint on the prediction. Neural generalization is the proposed way to transfer experience to such positions; it is an additional source of approximation, not part of the exact theorem.

Outcome training therefore supplies the evaluation half of the loop. A lower value loss is useful to the extent that it improves predictions at positions future search uses to compare actions. Accurate prediction of a weak policy, without a subsequent decision update, still reproduces weak play.

### 6.6. Replace the exact decision update with search, and retain its work in a prior

The exact loop selects an action maximizing $-V^{\mu_k}(T(s,a))$. We could approximate this directly by evaluating every immediate successor with $v_{\theta_{k+1}}$. The existing engine instead uses finite-budget MCTS: it investigates selected continuations beyond one ply and uses their accumulated evidence to choose a move. This is an additional algorithmic choice, whose mechanics and limits were studied in Chapters 4–5. MCTS is not literally the greedy update used in the theorem.

This is why a better value predictor can change $\mu_\theta$: it changes the comparisons and subsequent investigations that determine the played move. Search can discover that a move's apparent value fails under a particular reply, or that an initially unpromising move has a useful continuation. Finite search can also miss those facts, so the connection is a mechanism for improvement rather than an automatic inequality between successive engines' strengths.

There is a second resource problem. Even when a search finds useful actions, a later search may spend its budget discovering the same choices again. A policy network can retain some of those action preferences and supply them cheaply as priors at future nodes. This role was unnecessary in the exact one-ply theorem, where every action was considered and all continuation values were supplied. It becomes useful when allocation of computation matters. Separating expensive planning from learning a fast approximation to its decisions is the organizing idea of Expert Iteration. [Anthony, Tian, and Barber, original Expert Iteration paper](https://arxiv.org/html/1705.08439v4)

A concrete choice of target is normalized root visits:

$$
\rho_j(a)=\frac{N(s_j,a)}{\sum_bN(s_j,b)}.
$$

This is a specified design choice, not a proof that counts equal optimal action probabilities. It preserves a distribution over the search's attention rather than reducing the target to one selected move. At finite budget it can reflect prior errors and incomplete exploration too.

We now have an ordinary probability-prediction problem: fit $p_\theta(\cdot\mid s_j)$ to the target distribution $\rho_j$. If an action A is sampled from $\rho_j$ and we score the prediction by $-\log p_\theta(A\mid s_j)$, the expected loss is

$$
\mathbb E_{A\sim\rho_j}[-\log p_\theta(A\mid s_j)]
=-\sum_a\rho_j(a)\log p_\theta(a\mid s_j).
$$

This explains the cross-entropy term. For unrestricted probability vectors,

$$
\operatorname{CE}(\rho,p)
=H(\rho)+\operatorname{KL}(\rho\|p).
$$

The entropy $H(\rho)$ is fixed by the target, and KL divergence is nonnegative with equality at $p=\rho$. Thus this loss has the desired target as its minimizer. A shared finite network can only fit the collection of targets approximately.

The outcome label and policy target answer different questions: the outcome evaluates what happened in the played game, while the visit distribution records what the search favored before that move was played. Neither is interchangeable with the other.

### 6.7. Combine training tasks only after their roles and targets are defined

For a dataset $D$ of triples $(s,\rho,z)$, a joint objective is

$$
\mathcal L(\theta)
=\frac1{|D|}\sum_{(s,\rho,z)\in D}
\left[
(v_\theta(s)-z)^2
-\lambda_p\sum_a\rho(a)\log p_\theta(a\mid s)
\right],\qquad\lambda_p>0.
$$

The value term follows from the outcome-prediction problem; the policy term follows from predicting the search distribution. The weight $\lambda_p$ sets their relative influence when parameters are shared. It is a training choice, not a consequence of minimax.

With finite data, a large function class can fit accidental features of its examples. One optional response is parameter regularization, such as adding $\lambda_r\|\theta\|^2$ with $\lambda_r\ge0$. This changes the fitting preference; it is not required to define the two prediction targets. Architecture, regularization strength, and optimization must be assessed as part of the training recipe.

In this construction, search generates labels and distributions. Training then fits the network to them. We do not need to differentiate through the tree's discrete selections to define this objective.

### 6.8. Close the practical loop and locate the hoped-for improvement

For frozen generations, the sequence is:

1. Use $\theta_k$ inside search to define the playing policy $\mu_k$.
2. Collect played positions, search distributions $\rho_k$, and eventual game outcomes.
3. Fit $v_{\theta_{k+1}}$ to outcomes and $p_{\theta_{k+1}}$ to search distributions.
4. Search again using those new functions, obtaining $\mu_{k+1}=\mathcal S_B[p_{\theta_{k+1}},v_{\theta_{k+1}}]$.

The intended relationships, on the represented data, are

$$
v_{\theta_{k+1}}\approx V^{\mu_k},
\qquad
p_{\theta_{k+1}}\approx\rho_k.
$$

If the played policy samples exactly from $\rho_k$, then $\rho_k=\mu_k$. Otherwise, copying the search distribution and copying the actual played policy are different objectives. In particular, a deterministic most-visited choice need not equal the normalized-visit distribution used for training.

The new value function predicts outcomes of the previous playing policy; it is not already an exact evaluator of the next policy. This is the same ordering used in the ideal loop: first evaluate available behavior, then use that evaluation to construct new behavior. There is no need to compute a circular definition or know optimal play before obtaining labels.

Where could additional strength come from? The previous search may have discovered useful choices absent from the old prior. Copying those choices can make them available with less search effort. More accurate continuation predictions can allow subsequent searches to compare actions more reliably and spend effort on remaining uncertainties. It is this next decision-making computation that attempts to exceed the previous playing policy; copying its behavior alone cannot do so.

Three different comparisons must therefore remain separate:

- A network fits its training targets better.
- A search-guided player makes stronger decisions than using its prior directly as a playing policy.
- The next search-guided player $\mu_{k+1}$ is stronger than the previous search-guided player $\mu_k$ at a stated resource budget.

The first two do not by themselves establish the third. Value errors, incomplete exploration, poor coverage, and imperfect imitation can break the exact loop's mechanism. Shared network updates can also alter previously useful predictions. Training loss measures fitting; evaluating engine strength requires games or other justified decision-quality measurements with specified opponents and budgets. This distinction lets us assess the intended improvement rather than assume it from a lower loss.

### 6.9. Address the coverage assumption when generating data

If every game repeats a narrow set of choices, training may never expose useful alternatives. This is a different problem from choosing the strongest move from one completed search. Exploration during data generation changes which examples future learning receives.

One choice is to sample played moves from visits with temperature $\tau>0$:

$$
\mu_\theta(a\mid s)
=\frac{N(s,a)^{1/\tau}}{\sum_b N(s,b)^{1/\tau}}.
$$

At least one count must be positive. At $\tau=1$, probabilities are proportional to counts. As $\tau\to0$, they concentrate on maximal-count actions; larger temperatures flatten the positive-count distribution. Actions with zero visits still get zero probability from this expression. Temperature changes the played move after search, rather than the next internal investigation during search.

Another choice changes the root search's prior before those visits are collected:

$$
\eta\sim\operatorname{Dirichlet}(\alpha,\ldots,\alpha),
\qquad
P'(s,a)=(1-\epsilon)P(s,a)+\epsilon\eta_a.
$$

Here $\alpha>0$ and $0\le\epsilon\le1$. The random vector and the mixture are probability distributions. This can direct investigations toward alternatives that the unmodified prior would have neglected. It changes search behavior; temperature changes subsequent action sampling. These are separate mechanisms and do not replace the internal PUCT exploration rule.

They can broaden data but also spend games on weaker choices. Their settings become part of the data-generating policy, so outcome targets depend on them. The policy target $\rho$ and actual playing policy $\mu$ also need not coincide when temperature or other transformations are applied. Neither mechanism guarantees adequate coverage of every important state at a finite budget.

### 6.10. Relate the construction to AlphaZero and Leela

AlphaZero uses MCTS-guided self-play, outcome-based value training, search-based policy targets, and a joint loss with regularization. It starts from randomly initialized parameters and updates continually. Our frozen generations expose the role of a fixed continuation policy; they are not a literal requirement of its training schedule. [Original AlphaZero algorithm and training description](https://arxiv.org/html/1712.01815v1)

With data from changing networks or settings, the regression target is the conditional outcome distribution represented in that data. Interpreting it as $V^\mu$ for one stationary policy requires extra assumptions. The framework $v_\theta(s)$ remains general; the data and objective determine what prediction it learns.

Lc0 follows the broad combination of learned priors, value evaluation, and MCTS/PUCT, with additional outputs and search mechanisms. A particular network's training targets depend on its trainer and provenance. We should not infer its full training recipe from the C++ engine's evaluation interface alone. [Lc0's algorithm primer](https://lczero.org/dev/lc0/search/alphazero/)

The mathematical argument explains why learning current-policy values can be part of a route to optimal play. The policy prior helps make repeated planning affordable; the data-generation mechanisms seek experience needed for useful comparisons. Approximation and finite resources determine whether these components actually improve the next engine. The following chapter studies concrete mechanisms that address those resource and decision-quality problems in this checkout.

## 7. Add practical mechanisms in response to specific problems

Chapter 6 explained why evaluation and renewed decision-making can support learning, and identified the gap between that exact argument and a finite-budget engine. We now examine practical search mechanisms that affect the quality and cost of those decisions. They can also change the searches and outcomes recorded as future training targets; a faster search or lower training loss alone does not establish a stronger learning loop.

The basic search model used one fixed bounded value prediction at a new node, exact terminal payoffs, sequential visits, a fresh tree per move, and an unlimited visit limit for its proof. Each departure below answers a problem those assumptions leave unresolved. These are implementation mechanisms, not additional steps proved by the learning theorem. The formulas and source references describe the classic implementation in this checkout; specific options can change its behavior.

### 7.1. A discovered forced result should not have to wait for its average

The counterexample in Section 5.4 exposed a weakness of averaging alone. If one legal reply proves a win for its chooser, keeping its parent's decision dependent only on historical averages discards usable logical information.

Track proven outcomes separately. One child proving a win suffices to prove a win for its choosing player. Proving a loss requires that every legal action loses; otherwise an unexamined alternative might save the position. Draw proofs similarly require ruling out a win and establishing an available draw under the adopted rules. These are minimax proof obligations, rather than statistical judgments about high Q.

This adds exact-outcome propagation alongside estimated-value backup. A network prediction of $+1$ is not sufficient to trigger such a proof. The information must come from rules, already established descendants, or an applicable exact solver such as a tablebase.

Lc0 has terminal flags, outcome bounds, and tablebase handling. It also has configurable draw-search heuristics, such as treating some twofold repetitions as draws; such a heuristic must be distinguished from an exact rules-based terminal result. Tablebase results also have domain and rule assumptions. See `ExtendNode`, `EnsureNodeTwoFoldCorrectForDepth`, and `MaybeSetBounds` in [classic/search.cc](../../src/search/classic/search.cc).

The final move rule can prioritize solved outcomes and then use visits and tie-breaking among other actions. That explains why the simple most-visited rule is not the whole implementation. See `GetBestChildrenNoTemperature` in the same file.

### 7.2. An unvisited action still needs a provisional selection value

$Q=W/N$ is undefined before the first visit. Assigning zero, as in our proof model, is a convention. At a position believed strongly favorable or unfavorable, zero can be an implausible comparison point for every unexplored move.

First-play urgency (FPU) supplies a provisional value until the action obtains observations. In a simplified view of this checkout's reduction mode,

$$
Q_{\mathrm{unvisited}}
=q_{\mathrm{parent}}
-r\sqrt{\sum_{a:\,N(s,a)>0}P(s,a)},\qquad r\ge0,
$$

where $q_{\mathrm{parent}}$ is expressed for the current choosing player. The parent value anchors the assumption to the current position; the reduction makes unexplored actions more pessimistic as visited actions account for more prior mass. An absolute-value mode is also available.

This is a temporary selection assumption, not an observed result, a prior visit, or a proven bound. It can affect finite-budget allocation substantially even though a bounded provisional value does not defeat the exploration argument for the basic positive-prior model. See `GetFpu` in [classic/search.cc](../../src/search/classic/search.cc); its signs reflect the implementation's node-storage convention.

The code can also change the exploration coefficient with node visits, using a logarithmic schedule of the form $c(N)=c_0+k\log((N+b)/b)$ for the configured parameters. That gives additional control over allocation as a search grows. It is a separate tuning choice; our fixed-c proof does not by itself justify its finite-budget settings. See `ComputeCpuct` in the same file.

### 7.3. A scalar outcome prediction can discard information useful to decision rules

With win/draw/loss probabilities $(w,d,\ell)$, the expected score for payoffs $(+1,0,-1)$ is $v=w-\ell$. This mapping loses information: a predicted certain draw and an equal mixture of wins and losses both have scalar value zero. **The purpose of this example is to show that scalar equality need not mean equal outcome distributions.**

Lc0 supports win/draw/loss outputs and a moves-left estimate. Keeping draw probability allows a decision rule to express a different draw utility, for example $w-\ell+\delta d$. That changes the specified utility; it should not be silently identified with the original zero-utility draw objective. Moves-left information can support preferences for faster wins or slower losses, subject to the chosen rule. [Lc0 value-head description](https://lczero.org/dev/lc0/search/alphazero/)

These are useful extensions of the prediction interface, not prerequisites for basic MCTS. The [backend interface](../../src/neural/backend.h) carries the relevant fields, and [classic/search.cc](../../src/search/classic/search.cc) contains the corresponding scoring adjustments. Predicted probabilities remain estimates rather than solved-outcome proofs.

### 7.4. Sequential visits can waste accelerator capacity

Modern evaluators can process batches efficiently. Gathering several leaves before a network call can increase throughput, but creates a new problem: later path selections have not yet seen the earlier pending results. Several selections can request the same unfinished work instead of gathering diverse useful leaves.

Track pending visits separately from completed evidence. If $I(s,a)$ counts in-flight work, an exploration denominator can use $1+N(s,a)+I(s,a)$. A path already being investigated then receives less immediate exploration pressure without inventing an observed value for it. Completed work updates its Q and releases the reservation; canceled work must also release reservations. Collisions require explicit handling.

The benefit is potentially more useful network work per unit time. The cost is selection using less current evidence and additional coordination. Increasing evaluated positions per second does not automatically improve move choices at a fixed wall-clock budget.

See `GatherMinibatch`, `CollectCollisions`, and `RunNNComputation` in [classic/search.cc](../../src/search/classic/search.cc), plus the in-flight counters in [node.h](../../src/search/classic/node.h) and [node.cc](../../src/search/classic/node.cc). This extends the sequential proof model and requires its own correctness and performance analysis.

### 7.5. Repeated positions and successive moves offer different reuse opportunities

Repeatedly evaluating equivalent inputs wastes network work. An evaluation cache can reuse a prediction. Advancing one actual move can also discard a large subtree that was already investigated; retaining the matching subtree can preserve those visits. These reuse opportunities have different requirements.

A cached prediction must correspond to the input abstraction intended by its key. Same piece placement alone need not imply equal castling rights, repetition information, history, or bughouse pockets. Cache-key omissions and hash collisions require deliberate treatment. See [neural/memcache.cc](../../src/neural/memcache.cc).

Tree reuse preserves search statistics rather than only a network result. `NodeTree::MakeMove` retains a matching child subtree and discards siblings. Its history and terminal assumptions must remain consistent with the actual game. The resulting playing policy can depend on retained search state, so the fresh-tree notation in Chapter 6 suppresses a dependency present in the implementation. See [classic/node.cc](../../src/search/classic/node.cc).

Merging transpositions into a graph goes further: different paths can share a node's statistics. That changes the relation between parent-edge visits and child-node visits used in our finite-tree proof, and introduces additional backup and history questions. This repository has separate `classic` and `dag_classic` implementations. Neither graph search nor its use for bughouse follows automatically from adding an evaluation cache.

### 7.6. A real game needs a stopping rule and a definition of computational cost

Our convergence proof grants indefinitely many visits. A game grants a clock, and spending time on one move leaves less for later moves. The engine must choose when the expected benefit of further investigation is worth its cost and return a legal decision when stopped.

Budgets can count elapsed time, visits, new expansions, network evaluations, or memory. They are not interchangeable. Cached or terminal visits need no new neural evaluation; a reused tree starts with existing work; batching changes wall-clock cost. A meaningful strength comparison must state which budget is held fixed.

Lc0 separates tree search from time management and stopping conditions. See [classic/wrapper.cc](../../src/search/classic/wrapper.cc), [time management](../../src/search/classic/stoppers/timemgr.h), and [search stoppers](../../src/search/classic/stoppers/stoppers.h). Their policies add finite-resource decisions that the selection formula alone does not specify.

### 7.7. Follow the conceptual responsibilities into this checkout

| Responsibility | Starting point |
|---|---|
| Set the actual position and launch search | `ClassicSearch` in [wrapper.cc](../../src/search/classic/wrapper.cc) |
| Coordinate one worker iteration | `ExecuteOneIteration` in [search.cc](../../src/search/classic/search.cc) |
| Select a path using current tree information | `PickNodesToExtendTask` in [search.cc](../../src/search/classic/search.cc) |
| Reconstruct a leaf and detect terminal conditions | `ExtendNode` in [search.cc](../../src/search/classic/search.cc) |
| Request and retrieve value and policy predictions | `ProcessPickedTask`, `FetchSingleNodeResult` in [search.cc](../../src/search/classic/search.cc) |
| Update averages and propagate solved information | `DoBackupUpdateSingleNode` in [search.cc](../../src/search/classic/search.cc) |
| Store statistics and retain a subtree | [node.h](../../src/search/classic/node.h), [node.cc](../../src/search/classic/node.cc) |
| Play training games and record their data | [selfplay/game.cc](../../src/selfplay/game.cc), [trainingdata](../../src/trainingdata/trainingdata.h) |

The conceptual questions now give a purpose to these code paths. Examining their options and interactions is further work; the table is not a claim that every implementation heuristic has been derived from the basic convergence proof.

## 8. Reopen the assumptions when moving from chess to bughouse

The preceding construction separated a game's definition, its optimal value, an approximate search, and an evaluation-and-improvement learning loop. Both convergence arguments depended on the game model: Chapter 5 used a finite alternating tree for search, and Chapter 6 used exact evaluation and greedy updates throughout such a tree. Bughouse adaptation must begin by specifying the corresponding decision process. If state transitions or decision order are wrong, learning and search can consistently solve the wrong modeled game.

### 8.1. Two local turns do not specify the next global decision

A two-board state can say whose turn it is on each board without saying which board's move happens next. If White on A and White on B can both act, they belong to opposing teams. Combining their actions into one maximization would give one decision-maker control over opposing teams' choices.

We must define what an action and a search step represent, including the treatment of event order, waiting, and timing. A chosen event schedule, a probabilistic event model, and a model with timing decisions can produce different transition and value equations. Choosing among them is a consequential modeling decision, not merely a change to move generation.

### 8.2. Value perspective must follow teams rather than ply parity

White on A followed by Black on B consists of moves by the same team. Negating a team value after every event would therefore be wrong. One possible representation measures values for a fixed team throughout; another changes perspective explicitly when the relevant decision-maker changes. The mathematical model must determine the conversion before we adapt selection and backup.

This is why the alternating-sign formula in Chapter 4 cannot be copied solely because the new tree also has parent and child nodes. Its proof used the alternation of opposing players.

### 8.3. Recheck information, endings, and algorithmic assumptions

State sufficiency now concerns both boards, pockets, promoted-piece information, relevant history, and whatever timing information the selected model needs. Terminal and draw rules must match the intended bughouse rules. For example, an ordinary-chess insufficient-material test on one board cannot by itself establish a global draw when partner-board captures can supply material. This is a concrete reason to revisit the terminal detector rather than only replace its board type.

After fixing that game model, we can ask which selection, evaluation, learning, cache, and reuse arguments remain valid. Neural priors and value targets must describe its actions and team outcomes; ordinary chess tablebases and rules cannot be assumed to apply.

For learning, we must identify what joint behavior generates the outcome labels, what alternatives the decision update compares, and what information each acting player may use. For the height-based argument in Chapter 6, we would also need a justified finite decision horizon and an appropriate optimality recurrence. Simultaneous events, waiting decisions, or partial information may require a different argument. Predicting team outcomes accurately is still an evaluation task; establishing how those predictions support better legal team decisions remains a separate obligation.

The next design task is therefore to define the bughouse decision process precisely enough to write its value equations. The document has explained tools for searching and learning within a model. It has not selected that model or established a state-of-the-art bughouse architecture.
