#include <R.h>
#include <Rdefines.h>
#include <time.h>

// support counting of sequences using memory-efficient
// prefix-trees. 
//
// fixme: free on user interrupt is impossible.
//
// (C) ceeboo 2007, 2015

typedef struct pnode {
    int index;
    int count;
    int visit;
    struct pnode *pl;
    struct pnode *pr;
} PN;

static PN *nq, **nb = NULL;		    // node pointers
static int npn, cpn, apn;		    // node counters

static void pnfree(PN *p) {
    if (p == NULL)
	return;
    pnfree(p->pl);
    pnfree(p->pr);
    free(p);
    apn--;
}

static void nbfree() {
    pnfree(*nb);
      free( nb);
    nb = NULL;
}

static PN *pnadd(PN *p, int *x, int n) {
    if (n == 0)
	return p;
    cpn++;
    if (p == NULL) {			    // append node
	p = nq = (PN *) malloc(sizeof(PN));
	if (p) {
	    apn++;
	    p->index = *x;
	    p->count = 0;
	    p->visit = 0;
	    p->pl = pnadd(NULL, x+1, n-1);
	    p->pr = NULL;
	} else
	    npn = 1;
    } else
    if (p->index == *x) {		    // existing node
	nq = p;
	p->pl = pnadd(p->pl, x+1, n-1);
    } else
    if (p->index < *x) {		    // search right subtree
	nq = p;
	p->pr = pnadd(p->pr, x, n);
    } else {				    // prepend node
	PN *q = nq = (PN *) malloc(sizeof(PN));
	if (q) {
	    apn++;
	    q->index = *x;
	    q->count = 0;
	    q->visit = 0;
	    q->pl = pnadd(NULL, x+1, n-1);
	    q->pr = p;
	    p = q;
	} else
	    npn = 1;
    }
    return p;
}


// retrieve count

static int pnget(PN *p, int *x, int n) {
    if (p == NULL || n == 0)
	return 0;
    cpn++;
    if (p->index == *x) {
	npn++;
	if (n == 1)
	    return p->count;
	return pnget(p->pl, x+1, n-1);
    }
    if (p->index < *x) 
	return pnget(p->pr, x, n);
    return 0;				    // set not found
}

// count sequence
//
// NOTE item indexes in itemsets must be in 
//	ascending order.
//

static int dpn, sn;
static int pnc;

static void pnscount(PN *p, int *x, int n) {
    if (p == NULL || n == 0)
	return;
    cpn++;
    // search
    if (p->index < *x) {	// *x > -1
	p = p->pr;
	while (p && p->index < *x) {
	    cpn++;
	    p = p->pr;
	}
	if (!p) {
	    if (!nq)
		return;
	    while (n && *x > -1) {
		x++;
		n--;
	    }
	    if (!n)
		return;
	    p = nq;
	}
    }
    if (p->index == *x) { 
	if (p->visit < sn) {
	    dpn++;
	    p->visit = sn + n;
	    if (pnc) {
		if (abs(p->count) < pnc)
		    p->count = pnc;
		else
		    if (p->count == pnc)
			p->count = -p->count;
	    } else
		p->count++;
	} else {
	    npn++;
	    if (p->visit == sn + n)
		return;		// x identical
	}
	if (p->index > -1) {
	    if (p->pl) {
		PN *q = nq;
		if (p->pl->index > -1)
		    nq = NULL;
		else
		    nq = p->pl;
		pnscount(p->pl, x+1, n-1);
		nq = q;
	    }
	    pnscount(p, x+1, n-1);
	} else {
	    nq = p->pl;
	    pnscount(nq, x+1, n-1);
	}
    } else			// p->index > -1
	if (*x > -1)
	    pnscount(p, x+1, n-1);
	else
	    if (nq)
		if (nq->index > -1)
		    pnscount(nq, x+1, n-1);
		else
		    pnscount(nq, x, n);
}

// map sequence
//
// NOTE itemset indexes must be 0-based
//	and item indexes must not be
//	negative
//
static int *eb = NULL;			    // buffer pointer
static int  ne;				    // buffer size

static void ebfree() {
    if (eb == NULL)
	return;
    free(eb);
    eb = NULL;
    ne = 0;
}

static int eballoc() {
    int *q = eb;
    if (!q)
	ne = 1024;			    // initial size
    else
	ne = ne * 2;
    eb = realloc(q, sizeof(int) * ne);
    if (!eb) {
	eb = q;
	ebfree();
	return 0;
    }
    return ne; 
}

static int emap(int *x, int nx, int *pe, int *ie) {
    int i, k, f, l, n;
    n = 0;
    if (pe) {
	for (i = 0; i < nx; i++) {
	    l = x[i];
	    f = pe[l];
	    l = pe[l+1];
	    if (n + l - f >= ne && !eballoc())	    // FIXME
		return 0;
	    for (k = f; k < l; k++)
		eb[n++] = ie[k];
	    eb[n++] = -1;			    // delimiter
	}
    } else {
	if (2 * nx > ne && !eballoc())
	    return 0;
	for (i = 0; i < nx; i++) {
	    eb[n++] = x[i];
	    eb[n++] = -1;			    // delimiter
	}
    }
    if (n)
	n--;
    return n;
}

SEXP R_pnscount(SEXP R_x, SEXP R_t, SEXP R_e, SEXP R_v) {
    if (!inherits(R_x, "sgCMatrix"))
	error("'x' not of class sgCMatrix");
    if (!inherits(R_t, "sgCMatrix"))
	error("'t' not of class sgCMatrix");
    if (INTEGER(GET_SLOT(R_x, install("Dim")))[0] != 
	INTEGER(GET_SLOT(R_t, install("Dim")))[0])
	error("the number of rows of 'x' and 't' do not conform");
    if (TYPEOF(R_v) != LGLSXP)
	error("'v' not of type logical");
    int i, f, l, k, n, nr, e;
    int *x = NULL;
    SEXP px, ix, pt, it;
    SEXP r; 
#ifdef _TIME_H
    clock_t t4, t3, t2, t1;

    t1 = clock();
    
    if (LOGICAL(R_v)[0] == TRUE)
	Rprintf("preparing ... ");
#endif
    nr = INTEGER(GET_SLOT(R_x, install("Dim")))[0];
    
    px = GET_SLOT(R_x, install("p"));
    ix = GET_SLOT(R_x, install("i"));

    pt = GET_SLOT(R_t, install("p"));
    it = GET_SLOT(R_t, install("i"));

    int *pe = NULL, *ie = NULL;
    if (!isNull(R_e)) {
        if (nr != INTEGER(GET_SLOT(R_e, install("Dim")))[1])
            error("the number of rows of 'x' and columns of 'e' do not conform");
        pe = INTEGER(GET_SLOT(R_e, install("p")));
        ie = INTEGER(GET_SLOT(R_e, install("i")));

	if (!eballoc())
	    error("buffer allocation failed");
    }

    cpn = apn = npn = 0;

    if (nb != NULL) 
	nbfree();
    nb = (PN **) malloc(sizeof(PN *) * (nr+1));
    if (nb == NULL)
	error("pointer array allocation failed");

    k = nr;
    nb[k] = NULL;
    while (k-- > 0)
	nb[k] = pnadd(nb[k+1], &k, 1);

    if (npn) {
	nbfree();
	error("node allocation failed");
    }

    f = 0;
    for (i = 1; i < LENGTH(px); i++) {
	l = INTEGER(px)[i];
	n = l-f;
	if (n == 0)
	    continue;
	n = emap(INTEGER(ix)+f, n, pe, ie);
	if (!n) {
	    nbfree();
	    ebfree();
	    error("buffer allocation failed");
	}
	x = eb;
	if (n > 1) {
	    pnadd(nb[*x], x, n);
	    if (npn) {
		nbfree();
		ebfree();
		error("node allocation failed");
	    }
	}
	f = l;
	R_CheckUserInterrupt();
    }

#ifdef _TIME_H
    t2 = clock();
    if (LOGICAL(R_v)[0] == TRUE) {
	Rprintf("%i sequences, created %i (%.2f) nodes [%.2fs]\n",
		LENGTH(px) - 1, apn, (double) apn / cpn,
		((double) t2 - t1) / CLOCKS_PER_SEC);
	Rprintf("counting ... ");
    }
#endif

    cpn = npn = dpn = sn = 0;
    pnc = 0;

    k = 0;
    f = 0;
    for (i = 1; i < LENGTH(pt); i++) {
	l = INTEGER(pt)[i];
	n = l-f;
	if (n == 0)
	    continue;
	k += n;
	n = emap(INTEGER(it)+f, n, pe, ie);
	if (!n) {
	    nbfree();
	    ebfree();
	    error("buffer allocation failed");
	}
	x = eb;
	sn++;
	nq = *nb;
	pnscount(nb[*x], x, n);
	sn += n;
	f = l;
	R_CheckUserInterrupt();
    }

#ifdef _TIME_H
    t3 = clock();
    if (LOGICAL(R_v)[0] == TRUE) {
	Rprintf("%i transactions (%i), processed %i (%.2f, %.2f) nodes [%.2fs]\n",
		k, LENGTH(pt) - 1, cpn, (double) dpn / cpn, (double)  npn / cpn,
		((double) t3 - t2) / CLOCKS_PER_SEC);
	Rprintf("writing ... ");
    }
#endif
 
    PROTECT(r = allocVector(INTSXP, LENGTH(px)-1));

    cpn = npn = 0;
    
    e = LENGTH(pt) - 1;
    f = 0;
    for (i = 1; i < LENGTH(px); i++) {
	l = INTEGER(px)[i];
	n = l-f;
	if (n == 0) {
	    INTEGER(r)[i-1] = e;
	    continue;
	}
	n = emap(INTEGER(ix)+f, n, pe, ie);
	if (!n) {			    // never
	    nbfree();
	    ebfree();
	    error("buffer allocation failed");
	}
	x = eb;
	n = pnget(nb[*x], x, n);
	INTEGER(r)[i-1] = n; 
	f = l;
	R_CheckUserInterrupt();
    }
  
    nbfree();
    ebfree();

    if (apn)
	error("node deallocation imbalance %i", apn);
    
#ifdef _TIME_H
    t4 = clock();

    if (LOGICAL(R_v)[0] == TRUE) {
	Rprintf("%i counts, ", LENGTH(px)-1);
	Rprintf("processed %i (%.2f) nodes [%.2fs]\n", cpn, (double) npn / cpn,
		((double) t4 - t3) / CLOCKS_PER_SEC);
    }
#endif

    UNPROTECT(1);

    return r;
}

// NOTE node statistics are omitted.
//

SEXP R_pnsclosed(SEXP R_x, SEXP R_e, SEXP R_c, SEXP R_v) {
    if (!inherits(R_x, "sgCMatrix"))
	error("'x' not of class sgCMatrix");
    if (TYPEOF(R_c) != INTSXP)
	error("'c' not of storage type integer");
    if (LENGTH(R_c) != INTEGER(GET_SLOT(R_x, install("Dim")))[1])
	error("'x' and 'c' not the same length");
    if (TYPEOF(R_v) != LGLSXP)
	error("'v' not of type logical");
    int i, f, l, k, n, nr, e;
    int *x = NULL;
    SEXP px, ix;
    SEXP r; 
#ifdef _TIME_H
    clock_t t4, t3, t2, t1;

    t1 = clock();
    
    if (LOGICAL(R_v)[0] == TRUE)
	Rprintf("checking ... ");
#endif
    nr = INTEGER(GET_SLOT(R_x, install("Dim")))[0];
    
    px = GET_SLOT(R_x, install("p"));
    ix = GET_SLOT(R_x, install("i"));

    int *pe = NULL, *ie = NULL;
    if (!isNull(R_e)) {
        if (nr != INTEGER(GET_SLOT(R_e, install("Dim")))[1])
            error("the number of rows of 'x' and columns of 'e' do not conform");
        pe = INTEGER(GET_SLOT(R_e, install("p")));
        ie = INTEGER(GET_SLOT(R_e, install("i")));

	if (!eballoc())
	    error("buffer allocation failed");
    }

    cpn = apn = npn = 0;

    if (nb != NULL) 
	nbfree();
    nb = (PN **) malloc(sizeof(PN *) * (nr+1));
    if (nb == NULL)
	error("pointer array allocation failed");

    k = nr;
    nb[k] = NULL;
    while (k-- > 0)
	nb[k] = pnadd(nb[k+1], &k, 1);

    if (npn) {
	nbfree();
	error("node allocation failed");
    }

    f = 0;
    for (i = 1; i < LENGTH(px); i++) {
	l = INTEGER(px)[i];
	n = l-f;
	if (n == 0)
	    continue;
	n = emap(INTEGER(ix)+f, n, pe, ie);
	if (!n) {
	    nbfree();
	    ebfree();
	    error("buffer allocation failed");
	}
	x = eb;
	if (n > 1) {
	    pnadd(nb[*x], x, n);
	    if (npn) {
		nbfree();
		ebfree();
		error("node allocation failed");
	    }
	}
	f = l;
	R_CheckUserInterrupt();
    }

#ifdef _TIME_H
    t2 = clock();
#endif

    cpn = npn = dpn = sn = 0;
    pnc = 0;

    // k = 0;
    e = 0;
    f = 0;
    for (i = 1; i < LENGTH(px); i++) {
	l = INTEGER(px)[i];
	n = l-f;
	if (n == 0)
	    continue;
	// k += n;
	n = emap(INTEGER(ix)+f, n, pe, ie);
	if (!n) {
	    nbfree();
	    ebfree();
	    error("buffer allocation failed");
	}
	pnc = INTEGER(R_c)[i-1];
	if (pnc > e)
	    e = pnc;
	else
	    if (pnc < 1) {
		nbfree();
		ebfree();
		error("invalid count");
	    }
	x = eb;
	sn++;
	nq = *nb;
	pnscount(nb[*x], x, n);
	sn += n;
	f = l;
	R_CheckUserInterrupt();
    }

#ifdef _TIME_H
    t3 = clock();
#endif

    PROTECT(r = allocVector(LGLSXP, LENGTH(px)-1));

    cpn = npn = 0;
    
    f = 0;
    for (i = 1; i < LENGTH(px); i++) {
	l = INTEGER(px)[i];
	n = l-f;
	if (n == 0) {
	    pnc = INTEGER(R_c)[i-1];
	    if (pnc < e) {
		nbfree();
		ebfree();
		error("invalid count");
	    }
	    LOGICAL(r)[i-1] = (pnc > e) ? TRUE : FALSE;
	    continue;
	}
	n = emap(INTEGER(ix)+f, n, pe, ie);
	if (!n) {			    // never
	    nbfree();
	    ebfree();
	    error("buffer allocation failed");
	}
	x = eb;
	n = pnget(nb[*x], x, n);
	LOGICAL(r)[i-1] = (n > 0) ? TRUE : FALSE;
	f = l;
	R_CheckUserInterrupt();
    }
  
    nbfree();
    ebfree();

    if (apn)
	error("node deallocation imbalance %i", apn);
    
#ifdef _TIME_H
    t4 = clock();

    if (LOGICAL(R_v)[0] == TRUE) {
	Rprintf("%i counts [%.2fs, %.2fs]\n ", LENGTH(px)-1,
		((double) t4 - t1) / CLOCKS_PER_SEC,
		((double) t3 - t2) / CLOCKS_PER_SEC);
    }
#endif

    UNPROTECT(1);

    return r;
}

