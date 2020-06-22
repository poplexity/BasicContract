/**
 *  @file
 *  @copyright (c) 2019 Copyright Transledger inc. All rights reserved 
**/

#include "basiccontract.hpp"

namespace eosio
{
    void BasicToken::create(name issuer,
                            asset maximum_supply)
    {
        require_auth(get_self());

        auto sym = maximum_supply.symbol;
        check(sym.is_valid(), "invalid symbol name");
        check(sym == WRAP_SYM, "can only create " + WRAP_SYM.code().to_string());
        check(maximum_supply.is_valid(), "invalid supply");
        check(maximum_supply.amount > 0, "max-supply must be positive");

        stats statstable(get_self(), sym.code().raw());
        auto existing = statstable.find(sym.code().raw());
        check(existing == statstable.end(), "token with symbol already exists");

        statstable.emplace(get_self(), [&](auto &s) {
            s.supply.symbol = maximum_supply.symbol;
            s.max_supply = maximum_supply;
            s.issuer = issuer;
        });
    }

    void BasicToken::issue(name to, asset quantity, string memo)
    {
        auto sym = quantity.symbol;
        check(sym.is_valid(), "invalid symbol name");
        check(memo.size() <= 256, "memo has more than 256 bytes");

        stats statstable(get_self(), sym.code().raw());
        auto existing = statstable.find(sym.code().raw());
        check(existing != statstable.end(), "token with symbol does not exist, create token before issue");
        const auto &st = *existing;

        require_auth(get_self());
        check(quantity.is_valid(), "invalid quantity");
        check(quantity.amount > 0, "must issue positive quantity");

        check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
        check(quantity.amount <= st.max_supply.amount - st.supply.amount, "quantity exceeds available supply");

        statstable.modify(st, same_payer, [&](auto &s) {
            s.supply += quantity;
        });

        add_balance(to, quantity, get_self());
    }

    void BasicToken::retire(asset quantity, string memo)
    {
        auto sym = quantity.symbol;
        check(sym.is_valid(), "invalid symbol name");
        check(memo.size() <= 256, "memo has more than 256 bytes");

        stats statstable(get_self(), sym.code().raw());
        auto existing = statstable.find(sym.code().raw());
        check(existing != statstable.end(), "token with symbol does not exist");
        const auto &st = *existing;

        require_auth(get_self());
        check(quantity.is_valid(), "invalid quantity");
        check(quantity.amount > 0, "must retire positive quantity");

        check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");

        statstable.modify(st, same_payer, [&](auto &s) {
            s.supply -= quantity;
        });

        sub_balance(st.issuer, quantity);
    }

    void BasicToken::unwrap(name from,
                            asset quantity)
    {
        require_auth(from);
        check(quantity.symbol == WRAP_SYM, "can only unwrap " + WRAP_SYM.code().to_string());
        // transfer from owner to self and then burn
        sub_balance(from, quantity);
        add_balance(get_self(), quantity, get_self());

        action(permission_level{get_self(), name("active")}, CORE_SYM_ACCOUNT, name("transfer"),
               make_tuple(get_self(),
                          from,
                          asset(quantity.amount, CORE_SYM),
                          std::string("Unwrap tokens")
                          ))
            .send();

        action(permission_level{get_self(), name("active")}, get_self(), name("retire"),
               make_tuple(quantity,
                          std::string("Burn unwrapped tokens")))
            .send();
    }

    void BasicToken::transfer(name from,
                              name to,
                              asset quantity,
                              string memo)
    {
        check(from != to, "cannot transfer to self");
        require_auth(from);
        check(is_account(to), "to account does not exist");
        auto sym = quantity.symbol.code();
        stats statstable(get_self(), sym.raw());
        const auto &st = statstable.get(sym.raw());

        require_recipient(from);
        require_recipient(to);

        check(quantity.is_valid(), "invalid quantity");
        check(quantity.amount > 0, "must transfer positive quantity");
        check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
        check(memo.size() <= 256, "memo has more than 256 bytes");

        auto payer = has_auth(to) ? to : from;

        sub_balance(from, quantity);
        add_balance(to, quantity, payer);
    }

    void BasicToken::transferfrom(name from,
                                  name to,
                                  name spender,
                                  asset quantity,
                                  string memo)
    {
        check(from != to, "cannot transfer to self");
        check(is_account(from), "from account does not exist");
        check(is_account(to), "to account does not exist");

        auto sym = quantity.symbol.code();
        stats statstable(get_self(), sym.raw());
        const auto &st = statstable.get(sym.raw());

        // Notify both the sender and receiver upon action completion
        require_recipient(from);
        require_recipient(to);
        require_recipient(spender);

        check(quantity.is_valid(), "invalid quantity");
        check(quantity.amount > 0, "must transfer positive quantity");
        check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
        check(memo.size() <= 256, "memo has more than 256 bytes");

        allowed allowedtable(get_self(), from.value);
        auto existing = allowedtable.find(spender.value + sym.raw()); //Find returns an iterator pointing to the found object
        check(existing != allowedtable.end(), "spender not allowed");
        const auto &at = *existing;

        require_auth(at.spender);
        check(at.quantity.is_valid(), "invalid allowed quantity");
        check(at.quantity.amount > 0, "allowed must be a positive quantity");
        check(at.quantity.symbol == st.supply.symbol, "symbol precision mismatch");
        check(at.quantity.amount >= quantity.amount, "Allowed quantity < Transfer Quantity");

        auto payer = has_auth(to) ? to : spender;

        sub_balancefrom(from, at.spender, quantity);
        add_balance(to, quantity, payer);
        if (at.quantity.amount == quantity.amount)
        {
            allowedtable.erase(at);
        }
        else
        {
            allowedtable.modify(at, at.spender, [&](auto &a) {
                a.quantity -= quantity;
            });
        }
    }

    void BasicToken::approve(name owner,
                             name spender,
                             asset quantity)
    {
        check(owner != spender, "cannot allow self");

        require_auth(owner);
        check(is_account(spender), "spender account does not exist");

        auto sym = quantity.symbol.code();
        stats statstable(get_self(), sym.raw());
        const auto &st = statstable.get(sym.raw());

        // Notify both the sender and receiver upon action completion
        require_recipient(owner);
        require_recipient(spender);

        check(quantity.is_valid(), "invalid quantity");
        check(quantity.amount > 0, "Must approve non-zero positive quantity");
        check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");

        accounts from_acnts(get_self(), owner.value);
        const auto &from = from_acnts.get(quantity.symbol.code().raw(), "no balance object found");

        // Making changes to allowed in owner scope
        allowed allowedtable(get_self(), owner.value);
        auto allow = allowedtable.find(spender.value + sym.raw()); //Find returns an iterator pointing to the found object
        // const auto &at = *existing;
        if (allow == allowedtable.end())
        {
            check(from.balance.amount >= quantity.amount, "Cannot approve more than balance");
            allowedtable.emplace(owner, [&](auto &a) {
                a.key = spender.value + sym.raw();
                a.spender = spender;
                a.quantity = quantity;
            });
        }
        else
        {
            check(from.balance.amount >= quantity.amount + allow->quantity.amount, "Cannot approve more than balance");
            allowedtable.modify(allow, owner, [&](auto &a) {
                a.quantity += quantity;
            });
        }
    }

    void BasicToken::unapprove(name owner,
                               name spender,
                               asset quantity)
    {
        check(owner != spender, "cannot allow self");

        require_auth(owner);
        check(is_account(spender), "spender account does not exist");

        auto sym = quantity.symbol.code();
        stats statstable(get_self(), sym.raw());
        const auto &st = statstable.get(sym.raw());

        // Notify both the sender and receiver upon action completion
        require_recipient(owner);
        require_recipient(spender);

        check(quantity.is_valid(), "invalid quantity");
        check(quantity.amount > 0, "Must approve non-zero positive quantity");
        check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");

        // Making changes to allowed in owner scope
        allowed allowedtable(get_self(), owner.value);
        auto allow = allowedtable.find(spender.value + sym.raw()); //Find returns an iterator pointing to the found object
        const auto &at = *allow;
        check(allow != allowedtable.end(), "No allowance corresponding to spender and symbol");
        if (quantity.amount >= at.quantity.amount)
        {
            allowedtable.erase(at);
        }
        else
        {
            allowedtable.modify(at, owner, [&](auto &a) {
                a.quantity -= quantity;
            });
        }
    }

    void BasicToken::sub_balance(name owner, asset value)
    {
        accounts from_acnts(get_self(), owner.value);

        const auto &from = from_acnts.get(value.symbol.code().raw(), "no balance object found");
        check(from.balance.amount >= value.amount, "overdrawn balance");

        from_acnts.modify(from, owner, [&](auto &a) {
            a.balance -= value;
        });
    }

    void BasicToken::sub_balancefrom(name owner, name spender, asset value)
    {
        accounts from_acnts(get_self(), owner.value);

        const auto &from = from_acnts.get(value.symbol.code().raw(), "no balance object found");
        check(from.balance.amount >= value.amount, "overdrawn balance");

        from_acnts.modify(from, spender, [&](auto &a) {
            a.balance -= value;
        });
    }

    void BasicToken::add_balance(name owner, asset value, name ram_payer)
    {
        accounts to_acnts(get_self(), owner.value);
        auto to = to_acnts.find(value.symbol.code().raw());
        if (to == to_acnts.end())
        {
            to_acnts.emplace(ram_payer, [&](auto &a) {
                a.balance = value;
            });
        }
        else
        {
            to_acnts.modify(to, same_payer, [&](auto &a) {
                a.balance += value;
            });
        }
    }

    void BasicToken::open(name owner, const symbol &symbol, name ram_payer)
    {
        require_auth(ram_payer);

        auto sym_code_raw = symbol.code().raw();

        stats statstable(get_self(), sym_code_raw);
        const auto &st = statstable.get(sym_code_raw, "symbol does not exist");
        check(st.supply.symbol == symbol, "symbol precision mismatch");

        accounts acnts(get_self(), owner.value);
        auto it = acnts.find(sym_code_raw);
        if (it == acnts.end())
        {
            acnts.emplace(ram_payer, [&](auto &a) {
                a.balance = asset{0, symbol};
            });
        }
    }

    void BasicToken::close(name owner, const symbol &symbol)
    {
        require_auth(owner);
        accounts acnts(get_self(), owner.value);
        auto it = acnts.find(symbol.code().raw());
        check(it != acnts.end(), "Balance row already deleted or never existed. Action won't have any effect.");
        check(it->balance.amount == 0, "Cannot close because the balance is not zero.");
        acnts.erase(it);
    }

    void BasicToken::ontransfer(name from, name to, asset quantity, string memo)
    {
        //get initial receiver contract
        name rec = get_first_receiver();

        //validate
        if (rec == CORE_SYM_ACCOUNT && from != get_self() && quantity.symbol == CORE_SYM)
        {
            accounts acnts(get_self(), from.value);
            auto acct = acnts.find(WRAP_SYM.code().raw());
            check(acct != acnts.end(), "cannot accept deposit without open account, call the open action first");
            asset wrapamount = asset(quantity.amount, WRAP_SYM);
            action(permission_level{get_self(), name("active")}, get_self(), name("issue"),
                   make_tuple(from,
                              wrapamount,
                              std::string("Wrap tokens")))
                .send();
        }
    }

} // namespace eosio