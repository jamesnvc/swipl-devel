% author: Tom Schrijvers
% email:  Tom.Schrijvers@cs.kuleuven.ac.be
% copyright: K.U.Leuven, 2004

:- module(chr_hashtable_store,
	[ new_ht/1,
	  lookup_ht/3,
	  insert_ht/3,
	  delete_ht/3,
	  value_ht/2
	]).

:- use_module(pairlist).
:- use_module(hprolog).
:- use_module(library(lists)).

initial_capacity(65536).

new_ht(HT) :-
	initial_capacity(Capacity),
	new_ht(Capacity,HT).

new_ht(Capacity,HT) :-
        functor(T1,t,Capacity),
        HT = ht(Capacity,0,Table),
        Table = T1.

lookup_ht(HT,Term,Values) :-
	term_hash(Term,Hash),
	int_lookup_ht(HT,Hash,Term,Values).

int_lookup_ht(HT,Hash,Key,Values) :-
	HT = ht(Capacity,_,Table),
	Index is (Hash mod Capacity) + 1,
	arg(Index,Table,Bucket),
	nonvar(Bucket),
	( Bucket = K-Vs,
	  K == Key ->	
	    Values = Vs
	;
	    lookup_eq(Bucket,Key,Values)
	).

insert_ht(HT,Term,Value) :-
	term_hash(Term,Hash),
	( int_lookup_ht(HT,Hash,Term,Values),
	  hprolog:memberchk_eq(Value,Values) ->
		true
	;
		int_insert_ht(HT,Hash,Term,Value)
	).

int_insert_ht(HT,Hash,Key,Value) :-
	HT = ht(Capacity,Load,Table),
	NLoad is Load + 1,
	setarg(2,HT,NLoad),
	Index is (Hash mod Capacity) + 1,
	arg(Index,Table,Bucket),
	( var(Bucket) ->
		Bucket = Key-[Value]
	; Bucket = K-Vs ->
		( K == Key ->
			setarg(2,Bucket,[Value|Vs])
		;
			setarg(Index,Table,[Key-[Value],Bucket])
		)
	; lookup_pair_eq(Bucket,Key,Pair) ->
		Pair = _-Vs,
		setarg(2,Pair,[Value|Vs])
	;
		setarg(Index,Table,[Key-[Value]|Bucket])
	).


lookup_pair_eq([P | KVs],Key,Pair) :-
	P = K-_,
	( K == Key ->
		P = Pair
	;
		lookup_pair_eq(KVs,Key,Pair)
	).

delete_ht(HT,Term,Value) :-
	term_hash(Term,Hash),
	( int_lookup_ht(HT,Hash,Term,Values),
	  hprolog:memberchk_eq(Value,Values) ->
		int_delete_ht(HT,Hash,Term,Value)
	;
		true
	).

int_delete_ht(HT,Hash,Key,Value) :-
	HT = ht(Capacity,Load,Table),
	NLoad is Load - 1,
	setarg(2,HT,NLoad),
	Index is (Hash mod Capacity) + 1,
	arg(Index,Table,Bucket),
	( Bucket = _-Vs ->
		delete(Vs,Value,NVs),
		( NVs == [] ->
			setarg(Index,Table,_)
		;
			setarg(2,Bucket,NVs)
		)
	; 
		lookup_pair_eq(Bucket,Key,Pair),
		Pair = _-Vs,	
		delete(Vs,Value,NVs),
		( NVs == [] ->
			pairlist:delete_eq(Bucket,Key,NBucket),
			setarg(Index,Table,NBucket)
		;
			setarg(2,Pair,NVs)
		)
	).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
value_ht(HT,Value) :-
	HT = ht(Capacity,_,Table),
	value_ht(1,Capacity,Table,Value).

value_ht(I,N,Table,Value) :-
	I =< N,
	arg(I,Table,Bucket),
	(
		nonvar(Bucket),
		( Bucket = _-Vs ->
			true
		;
			member(_-Vs,Bucket)
		),
		member(Value,Vs)
	;
		J is I + 1,
		value_ht(J,N,Table,Value)
	).
		 	
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
term_hash(Term,Hash) :-
	hash_term(Term,Hash).
	

chr_delete([], _, []).
chr_delete([H|T], X, L) :-
        (   H==X ->
            chr_delete(T, X, L)
        ;   L=[H|RT],
            chr_delete(T, X, RT)
        ).
