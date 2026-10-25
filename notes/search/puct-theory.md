# Optional study: PUCT theory, regret bounds, and alternatives

This branches from [Section 5 of Search from first principles](01-search-from-first-principles.md). It can be studied after the basic mechanics of MCTS, PUCT, and self-play. It separates mathematical guarantees, useful interpretations, and empirical questions. No choice of search algorithm for the bughouse engine is made here.

## 1. Specify the objective before asking for a regret bound

Start with a flat stochastic bandit: there are $K$ actions, and each action $a$ has an independent sequence of identically distributed rewards in $[0,1]$ with unknown mean $\mu_a$. Define

$$
\mu^*=\max_a\mu_a,\qquad\Delta_a=\mu^*-\mu_a.
$$

Two distinct objectives are relevant:

$$
\mathcal R_T
=\mathbb E\!\left[\sum_{t=1}^T(\mu^*-\mu_{A_t})\right]
=\sum_a\Delta_a\,\mathbb E[N_T(a)]
\qquad\text{(cumulative pseudo-regret)},
$$

$$
r_T=\mathbb E[\mu^*-\mu_{\hat a_T}]
\qquad\text{(expected simple regret)}.
$$

Cumulative regret counts the cost of every exploratory action. Simple regret assesses only the final recommendation $\hat a_T$. The latter is closer to choosing a chess move after spending a search budget. A bound on one is not automatically the same bound on the other; the recommendation rule matters.

For chess, our ultimate reference is $Q^*(s,a)$ rather than an arm's stationary reward mean. A distribution of outcomes obtained by weak-policy continuations can have a stationary mean that differs from $Q^*$. Even a correct bandit theorem about that mean need not solve the chess problem.

## 2. Why UCB has its particular exploration term

For independent rewards in $[0,1]$, Hoeffding's inequality bounds the probability that an empirical average exceeds its true mean by $\epsilon$:

$$
\Pr(\bar X_n-\mu\ge\epsilon)\le e^{-2n\epsilon^2}.
$$

Solving $e^{-2n\epsilon^2}=\delta$ gives a radius $\sqrt{\log(1/\delta)/(2n)}$. Choosing a time-dependent failure level of order $t^{-4}$ produces the familiar radius $\sqrt{2\log t/n}$.

After initializing each action with a sample, UCB1 selects an action maximizing

$$
\bar X_a+\sqrt{\frac{2\log t}{N_t(a)}}.
$$

The bonus has a concentration-based motivation: investigate actions whose true means could plausibly exceed the currently best estimate. The adaptive choice of sample counts requires additional argument; applying a fixed-$n$ inequality naively at a random count is not a complete regret proof.

Under the independent, stationary, bounded-reward model, UCB1 satisfies

$$
\mathcal R_T
\le
8\sum_{a:\Delta_a>0}\frac{\log T}{\Delta_a}
+\left(1+\frac{\pi^2}{3}\right)\sum_a\Delta_a.
$$

The proof bounds how often a suboptimal action is selected: after roughly $8\log T/\Delta_a^2$ samples, selecting it requires a concentration failure. Summing those failure probabilities gives the additive constant. This is a gap-dependent cumulative-regret bound; its constants refer to rewards in $[0,1]$. Rescaling chess payoffs changes the numerical constants. [Auer, Cesa-Bianchi, and Fischer, Theorem 1 and its proof](https://homes.di.unimi.it/~cesabian/Pubblicazioni/ml-02.pdf)

For a concrete connection to simple regret, assume a unique best action and recommend a most-visited action. A wrong recommendation implies that at least half of the samples were suboptimal: the recommended suboptimal action has at least as many visits as the optimal one. Markov's inequality then gives

$$
\Pr(\hat a_T\ne a^*)
\le\frac{2}{T}\sum_{a\ne a^*}\mathbb E[N_T(a)],
$$

and hence

$$
r_T\le\Delta_{\max}\frac{2}{T}
\sum_{a\ne a^*}\mathbb E[N_T(a)].
$$

This is one valid route from action-count bounds to a final recommendation bound in that flat model. It is generally loose and does not transfer unchanged to Leela's tree, priors, value predictions, or additional move-selection rules.

## 3. PUCT changes the allocation rule; it does not inherit the UCB1 theorem

For the basic fixed-$c$ PUCT rule, write

$$
S_t(a)=Q_t(a)+cP_a\frac{\sqrt t}{1+N_t(a)}.
$$

Compare its bonus with UCB1's. PUCT includes a prior and uses $\sqrt t/(1+n)$; UCB1 uses $\sqrt{\log t/n}$. These are different functions of the counts. The UCB1 concentration argument cannot simply be relabeled as a PUCT proof.

A simple balance calculation illustrates the distinction. Consider deterministic arm rewards, positive fixed priors, and a unique best mean; initialize the arms so their empirical means are known exactly. If the optimal action receives most visits, its bonus tends to zero. For a suboptimal action to remain competitive, its bonus must roughly compensate for its gap:

$$
cP_a\frac{\sqrt t}{1+N_t(a)}\approx\Delta_a,
\qquad
N_t(a)\approx\frac{cP_a\sqrt t}{\Delta_a}.
$$

This suggests square-root rather than logarithmic allocation to suboptimal actions in this toy regime. It is a heuristic balance calculation, not a finite-time theorem: it assumes an allocation regime and ignores discrete scheduling and lower-order terms. It nevertheless shows why a logarithmic UCB1 bound cannot be assumed merely because both rules add an exploration bonus.

The prior also matters. A very small prior can delay investigating a critical defense under a finite budget. If an action has zero prior, this bonus supplies no exploration pressure for it; whether it is visited then depends on initialization, provisional values, and other rules. Positive priors or deliberate exploration mechanisms address a real coverage question, not just a numerical detail.

Leela can vary the coefficient with visits and account for pending evaluations. Any theorem or asymptotic calculation must specify the exact rule it studies. This chapter's displayed basic formula is not a claim about every configured classic-search variant.

## 4. Another mathematical interpretation: regularized allocation

There is a useful interpretation of the PUCT expression that does not identify its bonus with a confidence radius. Freeze the current search values $q_a$ and prior $p_a>0$. Let $r$ be a probability distribution allocating attention across actions. Consider

$$
J(r)=\sum_a r_aq_a-\lambda\operatorname{KL}(p\|r),
\qquad
\operatorname{KL}(p\|r)=\sum_a p_a\log\frac{p_a}{r_a}.
$$

The first term favors high-value actions. The penalty discourages starving actions to which the prior assigns mass. Holding $q,p,\lambda$ fixed, the coordinate derivative on the positive domain is

$$
\frac{\partial J}{\partial r_a}=q_a+\lambda\frac{p_a}{r_a}.
$$

Use smoothed visit proportions

$$
r_a=\frac{1+N_t(a)}{t+K},
\qquad\lambda_t=\frac{c\sqrt t}{t+K}.
$$

Substitution gives exactly the basic PUCT score:

$$
q_a+\lambda_t\frac{p_a}{r_a}
=q_a+c p_a\frac{\sqrt t}{1+N_t(a)}.
$$

This identifies the score with a marginal incentive of a prior-regularized objective. It does not assert that one discrete visit exactly maximizes that objective: updating counts changes the whole normalized distribution, and search values also change. Grill and colleagues develop the relationship between visit allocation and this regularized policy-optimization problem. [Grill et al., Sections 3.1–3.3](https://proceedings.mlr.press/v119/grill20a/grill20a.pdf)

This is an interpretation of why the terms fit together, rather than a proof of minimax correctness, calibrated confidence, or universal superiority. The KL direction matters: reversing it generally gives a different optimization problem and allocation.

## 5. Why tree-search guarantees need separate assumptions

In a flat stationary bandit, action $a$ repeatedly samples the same reward distribution. In tree search, a visit through an action can encounter different descendants, different depths, changing decisions by both players, and terminal results mixed with network predictions. The resulting means can drift as the tree grows.

UCT research explicitly analyzes tree-specific payoff drift and convergence under its stated setting. Its original finite-horizon MDP result is not, by itself, a theorem for arbitrary learned leaf values in adversarial chess. [Kocsis and Szepesvari, Theorem 6 and its assumptions](https://aima.cs.berkeley.edu/~russell/classes/cs294/s11/readings/Kocsis%2BSzepesvari%3A2006.pdf)

Consistency also does not imply useful finite-budget performance. There are deliberately constructed tree environments with extremely poor discovery times for UCT and related AlphaGo-style selection variants. These are counterexamples to blanket guarantees, not predictions of typical chess performance or a claim about every Leela configuration. [Orseau and Munos, 2024](https://arxiv.org/abs/2405.04407)

Before applying a bound to our engine, identify its assumptions about payoff generation, depth, prior support, leaf evaluation error, exploration, backup, stopping, and final action recommendation. A result about one of these settings cannot silently substitute for a result about another.

Sections 5.2–5.4 of [the main note](01-search-from-first-principles.md) give a self-contained qualitative convergence proof for a sequential, finite-tree, positive-prior version of basic PUCT. Its assumptions permit eventual exploration all the way to terminal states and dilution of initial network predictions. That supplies an elementary example of when average backups can converge to minimax, while leaving the finite-budget regret and implementation questions above unresolved.

## 6. Compare alternatives by the objective they address

The useful question is which conditions favor each method. An algorithm is not worse merely because it differs from PUCT.

| Method | Motivation | Limitation or question to examine |
|---|---|---|
| Greedy estimated value | Spend on the currently best-looking choice | Can stop revisiting an action underestimated early |
| Uniform sampling | Cover actions without trusting a prior | Can spend heavily on clearly inferior actions |
| UCB1 / UCT-style selection | Use optimism motivated by concentration | Flat guarantees require adaptation to changing tree payoffs; no learned prior in the basic index |
| PUCT | Allocate visits using learned priors and search evidence | Sensitive to prior quality, budget, coefficient, and leaf error |
| Best-action identification methods | Spend a fixed budget to make one final recommendation | Their stationary-arm guarantees still need adaptation to tree search |
| Selective minimax, with possible alpha-beta pruning | Retain adversarial backups and avoid irrelevant work | Requires a separate expansion or move-ordering strategy; boundary predictions can still be wrong |

For example, suppose a correct high-quality prior concentrates on useful actions. A prior-guided method has information that uniform sampling lacks. If the prior instead nearly suppresses the only saving move, uniform coverage may discover it earlier. These are mechanisms and conditional examples, not measurements ranking full engines.

Best-action identification is a separate research objective with algorithms and bounds specifically for fixed-confidence or fixed-budget recommendations. It deserves attention because search explores hypothetical moves in order to choose one real move. [Karnin, Koren, and Somekh, Almost Optimal Exploration in Multi-Armed Bandits](https://proceedings.mlr.press/v28/karnin13.html)

Alpha-beta pruning can retain the result of a specified minimax calculation while avoiding branches that cannot affect it, given valid bounds. That is different from a heuristic decision to omit a branch based only on a promising network score. Comparing it with MCTS requires holding the evaluation task and budget definition clear.

## 7. What would establish better performance for this project?

No bughouse ranking follows from the analysis above. A fair comparison should specify whether it measures final-move error on solved problems, playing strength, useful neural evaluations per decision, or another objective.

Useful later comparisons include controlled bandits, small trees with exact minimax answers, tactical trees with rare defenses, deliberately good and misleading priors, and finally engine matches. In the tree experiments, use the same evaluator and state transitions so a change in selection is not confused with a change in network strength. Report both neural-evaluation budgets and wall-clock budgets when they differ.

Such experiments would test hypotheses about our implementation. They have not been run as part of adding this chapter, and no alternative is labeled empirically inferior for bughouse here.

## 8. Suggested study order

1. Distinguish simple regret, cumulative regret, and exact minimax value.
2. Derive the UCB confidence bonus and work through the scope of its finite-time theorem.
3. Examine PUCT's different visit allocation, including good and bad priors.
4. Work through the regularization interpretation and its limits.
5. Study tree-specific convergence assumptions and hard examples.
6. Compare alternatives under matching objectives, then connect the analysis to this checkout's concrete options.

The theory branch can be extended with proof exercises and counterexamples that establish specific claims as we reach each topic. The main note can continue from Section 5 into the learning and implementation chapters without requiring this whole branch first.
