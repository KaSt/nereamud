"""
currency_olc.py

Extends oedit and medit with an option for editing their worths.
"""
import currency, olc



################################################################################
# olc interface
################################################################################
def currolc_menu(sock, thing):
    """Display a list of denominations, and the thing's current value for each.
    """
    dens = currency.denominations.keys()
    dens.sort()

    buf = [ ]
    i   = 1
    for den in dens:
        val = thing.worth[den]
        if val == 0:
            val = None
        buf.append("{g%2d) %-12s : {y[{c%6s{y]" % (i, den, str(val)))
        i += 1
    sock.send("\r\n".join(buf))
    
def currolc_chooser(sock, thing, option):
    """Try to pick a denomination to set.
    """
    try:
        opt = int(option) - 1
        if opt < 0 or opt >= len(currency.denominations):
            return olc.MENU_CHOICE_INVALID
        sock.send("Enter new value: ", newline=False)
        return opt + 1
    except:
        return olc.MENU_CHOICE_INVALID

def currolc_parser(sock, thing, choice, arg):
    """After picking a denomination to set, parse which value that denomination
       is to be set to. Do not allow negative values.
    """
    try:
        val = int(arg)
        if val < 0:
            return False
        dens = currency.denominations.keys()
        dens.sort()
        currency.set_denomination(thing, dens[choice-1], val)
        return True
    except: return False



################################################################################
# extender interface
################################################################################
def currolc_extend_menu(sock, thing):
    """Display the thing's value on the main medit or oedit screen.
    """
    sock.send("Edit value")
    if len(thing.worth) > 0:
        sock.send("{c%s{n" % str(thing.worth))

def currolc_choice_exec(sock, thing):
    """When the editor chooses to edit the thing's worth, push them into the
       currency OLC.
    """
    olc.do_olc(sock, currolc_menu, currolc_chooser, currolc_parser, None, thing)
    return olc.MENU_NOCHOICE

def currolc_to_proto(thing):
    """Return a Python statement embedded in a prototype that will set the
       thing's worth.
    """
    return "me.worth = %s\n" % str(thing.worth.values) 



################################################################################
# initialization
################################################################################
olc.extend("medit", "V", currolc_extend_menu, currolc_choice_exec, None, None,
           currolc_to_proto)
olc.extend("oedit", "V", currolc_extend_menu, currolc_choice_exec, None, None,
           currolc_to_proto)
