//
// Created by thakur on 3/2/24.
//

#ifndef COMPENSATOR_H
#define COMPENSATOR_H

#define E2FSPROGS_VERSION "1.47.0"
#define E2FSPROGS_DATE "5-Feb-2023"


enum quota_type {
	USRQUOTA = 0,
	GRPQUOTA = 1,
	PRJQUOTA = 2,
	MAXQUOTAS = 3,
};

#define QUOTA_USR_BIT (1 << USRQUOTA)
#define QUOTA_GRP_BIT (1 << GRPQUOTA)
#define QUOTA_PRJ_BIT (1 << PRJQUOTA)
#define QUOTA_ALL_BIT (QUOTA_USR_BIT | QUOTA_GRP_BIT | QUOTA_PRJ_BIT)


#endif //COMPENSATOR_H
