//
// obj2c: the data in a DOS object file, as a C array.
//
// The palette (OBJ/GAMEPAL.OBJ) and the signon screen (OBJ/SIGNON.OBJ) were
// linked into WOLF3D.EXE as Borland object files, and only those -- the
// sources they were made from are not in the release. Each holds one
// public symbol in one segment, its bytes in LEDATA records. This reads the
// OMF records, puts every LEDATA's bytes at its offset, and writes them out
// as a byte array, so the build uses the object files exactly as shipped.
//
//	obj2c FILE.OBJ name > name.c
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main (int argc, char **argv)
{
	FILE			*f;
	unsigned char	*obj,*data;
	long			size,i,end = 0;

	if (argc != 3)
	{
		fprintf (stderr,"usage: obj2c FILE.OBJ name\n");
		return 1;
	}

	f = fopen (argv[1],"rb");
	if (!f)
	{
		perror (argv[1]);
		return 1;
	}
	fseek (f,0,SEEK_END);
	size = ftell (f);
	rewind (f);
	obj = malloc (size);
	data = calloc (1,0x10000);
	if (!obj || !data || fread (obj,1,size,f) != (size_t)size)
	{
		fprintf (stderr,"obj2c: cannot read %s\n",argv[1]);
		return 1;
	}
	fclose (f);

	for (i = 0; i + 3 <= size; )
	{
		int		type = obj[i];
		long	len = obj[i+1] | (obj[i+2] << 8);
		unsigned char	*rec = obj + i + 3;

		if (i + 3 + len > size)
		{
			fprintf (stderr,"obj2c: %s: record runs past the end\n",argv[1]);
			return 1;
		}

		if (type == 0xa0)		// LEDATA: segment index, offset, bytes, checksum
		{
			long	offset = rec[1] | (rec[2] << 8);
			long	n = len - 4;

			if (offset + n > 0x10000)
			{
				fprintf (stderr,"obj2c: %s: data past 64K\n",argv[1]);
				return 1;
			}
			memcpy (data + offset,rec + 3,n);
			if (offset + n > end)
				end = offset + n;
		}

		i += 3 + len;
	}

	printf ("// Generated from %s by tools/obj2c.c. Do not edit.\n\n",argv[1]);
	printf ("unsigned char %s[%ld] =\n{",argv[2],end);
	for (i = 0; i < end; i++)
		printf ("%s%d,",i % 20 ? "" : "\n\t",data[i]);
	printf ("\n};\n");

	return 0;
}
