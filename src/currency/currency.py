"""
currency.py

Supports denominations for multiple currency types. Currency does not
necessarily have to be money; it can be any 'spendable' thing, such as
questpoints, kill tokens, experience, faction favor, etc.

Currency is supported by a matching item type; when items of the currency type
are picked up or received, they are immediately turned into the relevant
currency, and extracted.
"""
import mudsys, mud, auxiliary, storage, hooks, utils
import obj as mudobj


            
################################################################################
# local variables
################################################################################

# maps a denomination name to its info
denominations = { }

# maps a currency to its base denomination, if any
currency_base = { }

# maps a currency type to its possible denominations
currency_denominations = { }



################################################################################
# module classes and content extensions
################################################################################
class Denomination:
    """Contains information about one type of denomination for a type of
       currency.
    """
    def __init__(s, name, currency, value = 1, shorthand = None):
        s.name      = name
        s.currency  = currency
        s.value     = value
        s.shorthand = name
        if shorthand != None:
            s.shorthand = shorthand

class Worth:
    """Contains information about a collection of currency denominations. Worths
       are immutable, but they can be added, subtracted, and compared. gt and lt
       comparisons are not strictly ordered. The property of reflexiveness is
       is lost when worths containing different currencies (e.g., money and
       questpoints) are compared.
    """
    def __init__(s, set = None):
        s.values = { }
        if type(set) == dict:
            for key, val in set.iteritems():
                if type(val) != int or type(key) != str:
                    raise Exception("Worth maps denomination names to integer counts.")
                if val == 0 or not key in denominations:
                    continue
                s.values[key] = val
        elif set != None and set.contains("pairs"):
            for one_set in set.readList("pairs").sets():
                den = one_set.readString("denomination")
                val = one_set.readInt("value")
                if val == 0 or not den in denominations:
                    continue
                s.values[den] = val

    def simplify(s):
        """Returns a copy of the worth, with all of its denominations reduced
           down to their simplest type. e.g., 1 gold becomes 10 silver, etc...
        """
        bases = { }
        for key, val in s.values.iteritems():
            if key in denominations:
                den  = denominations[key]
                base = currency_base[den.currency]
                if not base.name in bases:
                    bases[base.name] = 0
                bases[base.name] += den.value * val
                if bases[base.name] == 0:
                    del bases[base.name]
        return Worth(bases)

    def __len__(s):
        """Returns the number of denominations in the worth."""
        return len(s.values)

    def __getitem__(s, denom):
        """Takes a denomination or a currency name. For a denomination, returns
           the count of that denomination within the worth. If a currency name
           is supplied, returns the sum of all denominations for it, in terms
           of the base denomination.
        """
        if denom in s.values:
            return s.values[denom]
        elif denom in currency_base and not denom in denominations:
            smpl = s.simplify()
            return smpl[currency_base[denom].name]
        return 0
    
    def __gt__(s, y):
        s = s.simplify()
        y = y.simplify()

        if len(y) > len(s):
            return False

        for denom in y.values.keys():
            if not denom in s.values:
                return False
            if y.values[denom] >= s.values[denom]:
                return False
        return True

    def __lt__(s, y):
        s = s.simplify()
        y = y.simplify()

        if len(s) > len(y):
            return False

        for denom in s.values.keys():
            if not denom in s.values:
                return False
            if s.values[denom] >= y.values[denom]:
                return False
        return True

    def __eq__(s, y):
        s = s.simplify()
        y = y.simplify()

        if len(s) != len(y):
            return False

        for denom in s.values.keys():
            if not denom in s.values:
                return False
            if s.values[denom] != y.values[denom]:
                return False
        return True

    def __ge__(s, y):
        return s > y or s == y

    def __le__(s, y):
        return s < y or s == y

    def __add__(s, y):
        if isinstance(y, Worth):
            y = y.values
        denoms = s.values.copy()
        for key, val in y.iteritems():
            if not key in denoms:
                denoms[key] = 0
            denoms[key] += val
        return Worth(denoms)

    def __sub__(s, y):
        if type(y) == Worth:
            y = y.values
        denoms = s.values.copy()
        for key, val in y.iteritems():
            if not key in denoms:
                denoms[key] = 0
            denoms[key] -= val
        return Worth(denoms)

    def __str__(s):
        if len(s) == 0:
            return "0"
        buf = [ ]
        for k, v in s.values.iteritems():
            if k in denominations:
                k = denominations[k].shorthand
            buf.append("%d %s" % (v, k))
        return ", ".join(buf)

    def copy(s):
        return Worth(s.values)

    def store(s):
        set   = storage.StorageSet()
        pairs = storage.StorageList()
        for den, val in s.values.iteritems():
            one_set = storage.StorageSet()
            one_set.storeString("denomination", den)
            one_set.storeInt("value", val)
            pairs.add(one_set)
        set.storeList("pairs", pairs)
        return set

class CurrencyAuxData:
    """Holds Worth information for one character."""
    def __init__(s, set = None):
        s.worth = Worth()
        if set != None and set.contains("worth"):
            s.worth = Worth(set.readSet("worth"))

    def copyTo(s, to):
        to.worth = s.worth

    def copy(s):
        newdata = CurrencyAuxData()
        s.copyTo(newdata)
        return newdata

    def store(s):
        set = storage.StorageSet()
        set.storeSet("worth", s.worth.store())
        return set



################################################################################
# properties
################################################################################
def __setWorth__(s, worth):
    if type(worth) == dict:
        worth = Worth(worth)
    if not isinstance(worth, Worth):
        raise Exception("Worth object not supplied")
    s.aux("currency_data").worth = worth

def __getWorth__(s):
    return s.aux("currency_data").worth

def set_denomination(s, denom, val):
    val -= s.aux("currency_data").worth[denom]
    s.aux("currency_data").worth += { denom : val }

def get_denomination(s, denom):
    return s.aux("currency_data").worth[denom]



################################################################################
# functions
################################################################################
def register_denomination(name, currency, value = 1, shorthand = None):
    """Create a new denomination for a specified type of currency. For example,
       'gold' for 'money'. If the currency is not yet created, create it. Each
       denomination has a value. For example, copper may be 1, silver may
       be 10, gold may be 100, and platinum may be 1000.
    """
    den = Denomination(name, currency, value, shorthand)
    denominations[name] = den
    if not currency in currency_denominations:
        currency_denominations[currency] = [ ]
    currency_denominations[currency].append(denominations[name])

    if value == 1:
        currency_base[currency] = den

    # add mobile and object properties for setting and getting
    g = lambda s   : get_denomination(s, name)
    s = lambda s, v: set_denomination(s, name, v)
    sh = shorthand
    if sh == None:
        sh = name
    mudsys.add_char_method(sh,property(g, s, doc = "The character's %s" % name))
    mudsys.add_obj_method(sh, property(g, s, doc = "The object's %s" % name))



################################################################################
# hooks
################################################################################
def get_currency_hook(info):
    """Turns a currency object into actual currency when taken.
    """
    ch, obj = hooks.parse_info(info)
    # if obj.hasBit("currency"):
    if utils.is_keyword(obj.bits, "currency"):
        ch.worth += obj.worth
        mud.extract(obj)

def give_currency_hook(info):
    """Turns a currency object into actual currency when received.
    """
    ch, recv, obj = hooks.parse_info(info)
    # if obj.hasBit("currency"):
    if utils.is_keyword(obj.bits, "currency"):
        recv.worth += obj.worth
        mud.extract(obj)



################################################################################
# initialization
################################################################################

# install currency aux data and item types
auxiliary.install("currency_data", CurrencyAuxData, "character, object")

# add hooks
hooks.add("get",  get_currency_hook)
hooks.add("give", give_currency_hook)

# add our character and object properties
mudsys.add_char_method("worth",
                       property(__getWorth__, __setWorth__,
                                doc="The character's accumulated wealth"))
mudsys.add_obj_method("worth",
                      property(__getWorth__, __setWorth__,
                               doc="The object's value"))

# items that have the currency bit will be transformed into worth when received
mudsys.create_bit("obj_bits", "currency")

# register default currency and denominations
## register_denomination("copper",   "money",     1, shorthand="cp")
## register_denomination("silver",   "money",    10, shorthand="sp")
## register_denomination("gold",     "money",   100, shorthand="gp")
## register_denomination("platinum", "money",  1000, shorthand="pp")

## register_denomination("questpoints", "questpoints", shorthand="qp")
## register_denomination("experience",  "experience",  shorthand="exp")
