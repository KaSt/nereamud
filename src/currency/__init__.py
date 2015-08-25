"""
package: currency

Contains auxiliary data for tracking player currency. Currency can be any
spendable value that has one or more denominations. As example, money might
have copper coins, silver coins, and gold coins. Questpoints might have quest
tokens. Also contains an implementation of a 'currency' item type. When currency
items are received, their value is added to the character's currency, and the
item is immediately extracted.
"""
import os

__all__ = [ ]

# compile a list of all our modules
for fl in os.listdir(__path__[0]):
    if fl.endswith(".py") and not (fl == "__init__.py" or fl.startswith(".")):
        __all__.append(fl[:-3])

# import all of our modules so they can register relevant mud settings and data
for module in __all__:
    exec "import " + module
