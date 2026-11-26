# Ship2Ship

## What is this game?

Ship2Ship runs Ocarina of Time and Majora's Mask side by side in one randomized world. Items found in one game can
open the other, and a single playthrough can use both. This Archipelago world is one slot for both games.

## What is randomized?

Every check of both games that Ship2Ship's own randomizer shuffles, as its settings choose. Items the games place
inside their own dungeons (their own keys, songs on song spots, rewards, shop buys) are fixed by the game and are not
Archipelago locations. Checks that nothing can ever reach with every item are left out.

## Where does the logic come from?

From Ship2Ship's generator, `comborando`, which this world asks for every access rule. There is no second copy of the
logic in Python, so the multiworld is beatable by the same rules the game plays by. The cost is that generating needs a
Ship2Ship install or the generator pack on the machine that generates (playing does not).

## What is the goal?

Defeat Ganon and Majora, or collect the required Triforce Pieces, as the `goal` option says. The slot always starts in
Ocarina of Time.

## What does an item from another world look like?

The game shows junk at that check and the client hands over the real item from the Archipelago server.
