#include <array>
#include <string.h>
#include "server.h"

static constexpr int MAX_FILTER_MESSAGE = 1000;
static constexpr std::array<unsigned int, 12> MONTH_DAYS = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

enum class FilterOp : unsigned char
{
	// actions
	Drop,
	// pure string comparison
	Match,
	// integer/string comparisons
	Eq,
	Neq,
	Lt,
	Lte,
	Gt,
	Gte,
	Max,
};

static constexpr std::array<const char *, static_cast<std::size_t>( FilterOp::Max )> opstr = {{
	"drop",
	"* ",
	"== ",
	"!= ",
	"< ",
	"<= ",
	"> ",
	">= ",
}};

union node_parm_t
{
	char *string;
	int integer;
};

struct filter_node_t
{
	filter_node_t *next;			// next node for current scope
	filter_node_t *child;			// action/child node
	char *p1;						// userinfo key or action message
	node_parm_t p2;
	FilterOp fop;
	bool is_date:1;					// p1 is a virtual date key
	bool is_fname:1;				// p1 is a filtered name field
	bool is_string:1;				// p2 contains string, not integer
	bool is_quoted:1;				// force string comparison
	bool is_cvar:1;					// p2 contains cvar name, should be dereferenced

	bool tagged:1;
};

static filter_node_t *nodes;

static std::array<char, MAX_FILTER_MESSAGE> filterMessage{};
static std::array<char, 64> filterDate{};  // current date string in "YYYY-MM-DD HH:mm" format
static std::array<char, 256> filterName{}; // filtered "name" userinfo key
static int  filterDateMsec;
static int  filterCurrMsec;
static int	nodeCount; // total count
static int	tempCount; // nodes that can expire
static int  expiredCount;


static void CleanStr( char *dst, int dst_size, const char *src )
{
	const char *max = dst + dst_size - 1;
	int	c;

	while ( (c = *src) != '\0' ) {
		if ( Q_IsColorString( src ) ) {
			src += 2;
			continue;
		} else if ( c >= ' ' && c <= '~' ) {
			*dst++ = c;
			if ( dst >= max )
				break;
		}
		src++;
	}

	*dst = '\0';
}


static const char *op2str( FilterOp op )
{
	const auto index = static_cast<std::size_t>( op );

	if ( index >= opstr.size() )
		return "? ";
	else
		return opstr[ index ];

}


static void free_nodes( filter_node_t *node )
{
	filter_node_t *next;
	while ( node != nullptr )
	{
		next = node->next;
		if ( node->child != nullptr )
		{
			free_nodes( node->child );
		}
		SV_ZFree( node );
		node = next;
	}
}


static int eval_node( const filter_node_t *node )
{
	if ( node->fop == FilterOp::Drop )
	{
		Q_strncpyz( filterMessage.data(), node->p1, SV_ArraySize(filterMessage) );
		return -1; // will break *->next node walk in parent
	}
	else
	{
		const char *value, *value2;
		int res = 0, v1, v2;

		if ( node->is_date )
		{
			if ( filterCurrMsec != filterDateMsec ) // update date string
			{
				qtime_t t;
				Com_RealTime( &t );
				Com_sprintf( node->p1, SV_ArraySize(filterDate), "%04i-%02i-%02i %02i:%02i",
					t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
					t.tm_hour, t.tm_min );
				filterDateMsec = filterCurrMsec;
			}
			value = node->p1;
		}
		else
		if ( node->is_fname )
		{
			if ( filterName[0] == '\0' )
			{
				CleanStr( filterName.data(), SV_ArraySize(filterName), Info_ValueForKeyToken( "name" ) );
			}
			//value = node->p1; // p1 points on filterName
			value = filterName.data();
		}
		else
		{
			value = Info_ValueForKeyToken( node->p1 ); 
		}

		if ( node->is_string )
		{
			value2 = node->p2.string;
			if ( node->is_cvar ) // dereference value2 
			{
				value2 = Cvar_VariableString( value2 + 1 );
			}

			if ( node->fop == FilterOp::Match )
			{
				res = Com_FilterExt( value2, value );
				return res; // early exit, just to silent compiler warnings about uninitialized v1 & v2
			}
			else
			{
				if ( node->is_quoted ) // forced string comparison
				{
					v1 = Q_stricmp( value, value2 );
					v2 = 0;
				}
				else // integer comparison
				{
					v1 = SV_ParseInt( value );
					v2 = SV_ParseInt( value2 );
				}
			}
		}
		else
		{
			v1 = SV_ParseInt( value );
			v2 = node->p2.integer;
		}

		switch ( node->fop )
		{
			//case FilterOp::Match: res = Com_FilterExt( value2, value ); break;
			case FilterOp::Eq:   res = (v1 == v2); break;
			case FilterOp::Neq:  res = (v1 != v2); break;
			case FilterOp::Lt:   res = (v1 <  v2); break;
			case FilterOp::Lte:  res = (v1 <= v2); break;
			case FilterOp::Gt:   res = (v1 >  v2); break;
			case FilterOp::Gte:  res = (v1 >= v2); break;
			default:       break;
		}
		return res;
	}
}


static void write_tabs( FILE *f, int count )
{
	for ( int ignored : SV_Indices( count ) ) {
		(void)ignored;
		fwrite( "\t", 1, 1, f );
	}
}


static void dump_nodes( const filter_node_t *node, int level, bool skip_tagged, FILE *f )
{
	std::array<char, MAX_TOKEN_CHARS + 32> buf{};
	int n;

	while ( node )
	{
		if ( node->tagged && skip_tagged )
		{
			node = node->next;
			continue;
		}

		write_tabs( f, level );

		if ( node->fop == FilterOp::Drop ) // final action
		{
			if ( *node->p1 )
			{
				n = Com_sprintf( buf.data(), SV_ArraySize(buf), "drop \"%s\"", node->p1 );
				fwrite( buf.data(), n, 1, f );
			} else
				fwrite( "drop", 4, 1, f );
		}
		else
		{
			const char *s = op2str( node->fop );

			if ( node->is_date )
			{
				if ( node->fop == FilterOp::Lt ) // do not print default action for dates
					s = "";
				n = Com_sprintf( buf.data(), SV_ArraySize(buf), "date %s\"%s\"", s, node->p2.string );
			}
			else
			{
				if ( node->fop == FilterOp::Eq ) // do not print default action for strings
					s = "";

				if ( node->is_string )
				{
					if ( node->is_quoted )
						n = Com_sprintf( buf.data(), SV_ArraySize(buf), "%s %s\"%s\"", node->p1, s, node->p2.string );
					else
						n = Com_sprintf( buf.data(), SV_ArraySize(buf), "%s %s%s", node->p1, s, node->p2.string );
				}
				else
				{
					n = Com_sprintf( buf.data(), SV_ArraySize(buf), "%s %s%i", node->p1, s, node->p2.integer );
				}
			}

			fwrite( buf.data(), n, 1, f );

			if ( node->child )
			{
				fwrite( " {\n", 3, 1, f );

				dump_nodes( node->child, level + 1, skip_tagged, f );

				fwrite( "\n", 1, 1, f );

				write_tabs( f, level );

				fwrite( "}", 1, 1, f );

				if ( node->next ) 
					fwrite( "\n", 1, 1, f );
			}

		}
		node = node->next;
//		if ( node && level == 0 )
//			fwrite( "\n", 1, 1, f );
	}
}


static int walk_nodes( const filter_node_t *node )
{
	while ( node != nullptr )
	{
		int res;
		if ( ( res = eval_node( node ) ) != 0 ) // evaluate current node
		{
			if ( res < 0 || ( res = walk_nodes( node->child ) ) < 0 )
			{
				return res;
			}
		}
		node = node->next;
	}

	return 0;
}


// marks specified node and its kids as expired
static void tag_from( filter_node_t *node )
{
	if ( node == nullptr )
		return;

	// current node
	node->tagged = true;

	// child nodes
	node = node->child;

	while ( node != nullptr )
	{
		if ( node->child )
		{
			tag_from( node->child );
		}

		node->tagged = true;
		node = node->next;
	}
}


static void clear_tags( filter_node_t *node )
{
	while ( node != nullptr )
	{
		if ( node->child )
		{
			clear_tags( node->child );
		}
		node->tagged = false;
		node = node->next;
	}
}


// try to find single expired date nodes
static int tag_expired( filter_node_t *node )
{
	while ( node != nullptr )
	{
		int res;

		if ( node->is_date && node->fop == FilterOp::Lt ) // it can expire
		{
			res = eval_node( node );
			if ( res == 0 )
			{
				tag_from( node );	// tag current and all descending nodes
				res = -2;			// skip any further evaluation
				expiredCount++;
			}
		}
		else
		{
			res = 1;
		}

		if ( res < 0 || ( res = tag_expired( node->child ) ) < 0 )
		{
			if ( res == -1 ) // action node
			{
				return res;
			}
		}

		node = node->next;
	}

	return 0;
}


// starting from root node
static bool tag_parents( filter_node_t *node )
{
	bool r = true; // masked value for all child nodes
	bool v;
	while ( node != nullptr )
	{
		if ( node->child ) 
			v = tag_parents( node->child );
		else 
			v = node->tagged;
		r = r && v;
		node->tagged = v;
		node = node->next;
	}
	return r;
}


static bool is_integer( const char *s )
{
	int n = 0;

	if ( *s == '-' )
		++s;

	while ( *s >= '0' && *s <= '9' )
	{
		++s; ++n;
	}

	if ( n == 0 || n > 24 || *s != '\0' )
		return false;
	else
		return true;
}


static filter_node_t *new_node( const char *p1, const char *p2, FilterOp fop, bool quoted )
{
	filter_node_t *node;
	int len, len1, len2;
	bool is_date = false;
	bool is_fname = false;

	// handle "date" key specially
	if ( Q_stricmp( p1, "date" ) == 0 && fop != FilterOp::Drop )
	{
		is_date = true;
		len1 = 0; // p1 will point on a static date buffer
		if ( !quoted )
		{
			COM_ParseError( "expecting quoted string with 'date' key" );
			return nullptr;
		}
	}
	else if ( Q_stricmp( p1, "fname" ) == 0 && fop != FilterOp::Drop )
	{
		is_fname = true;
		len1 = 0; // p1 will point on a static filtered name buffer
		if ( !quoted )
		{
			COM_ParseError( "expecting quoted string with 'fname' key" );
			return nullptr;
		}
	}
	else
	{
		len1 = strlen( p1 ) + 1; // key name or action message
		if ( len1 > MAX_FILTER_MESSAGE ) 
			len1 = MAX_FILTER_MESSAGE;
	}

	// right value
	if ( quoted || is_fname || is_date || !is_integer( p2 ) )
		len2 = strlen( p2 ) + 1; // string value
	else
		len2 = 0; // integer or null value

	len = len1 + len2 + sizeof( *node );
	node = SV_ZMallocBytes<filter_node_t>( len );
	*node = {};

	node->fop = fop;
	node->is_date = is_date;
	node->is_fname = is_fname;

	if ( is_date )
	{
		// point on static date buffer
		node->p1 = filterDate.data(); 
		if ( fop == FilterOp::Lt )
			tempCount++; // check for potential expire
	}
	else if ( is_fname )
	{
		// point on static filtered name buffer
		node->p1 = filterName.data(); 
	}
	else
	{
		// left value (key/action message)
		node->p1 = reinterpret_cast<char *>( node + 1 );
		Q_strncpyz( node->p1, p1, len1 );

		// if not action - convert key name to lowercase
		if ( fop != FilterOp::Drop )
			Q_strlwr( node->p1 );
	}

	if ( len2 ) // quoted or non-integer value
	{
		node->is_string = true;
		node->is_quoted = quoted;

		if ( p2[0] == '$' && p2[1] != '$' && p2[1] != '\0' )
			node->is_cvar = true; // needs to be defererenced at runtime

		if ( len1 )
			node->p2.string = node->p1 + len1;
		else
			node->p2.string = reinterpret_cast<char *>( node + 1 );

		Q_strncpyz( node->p2.string, p2, len2 );
	}
	else // integer/action parameter
	{
		node->p2.integer = SV_ParseInt( p2 );
	}

	nodeCount++; // statistics

	return node;
}


static const char *parse_section( const char *text, int level, filter_node_t **root, bool in_scope )
{
	filter_node_t *curr, *ch;
	FilterOp fop;
	std::array<char, 256> lvalue{};
	const char *v0, *back;
	tokenType_t op;

	curr = nullptr;

	for ( ;; )
	{
		// expecting new key/action
		v0 = COM_ParseComplex( &text, SV_QBool( in_scope ) );
		if ( com_tokentype == TK_EOF ) 
			break;

		// we are in child inline node
		if ( com_tokentype == TK_NEWLINE )
		{
			if ( curr == nullptr ) 
			{
				COM_ParseError( "unexpected newline" );
				return nullptr;
			}
			break; // exit from child node
		}

		 // leave current section
		if ( *v0 == '}' && curr && in_scope && level )
			break;

		if ( com_tokentype != TK_STRING /*&& com_tokentype != TK_QUOTED*/ )
		{
			COM_ParseError( "unexpected key/action '%s'", v0 );
			return nullptr;
		}

		if ( Q_stricmp( v0, "drop" ) == 0 )
		{
			fop = FilterOp::Drop;
			back = text; // backup
			v0 = COM_ParseComplex( &text, qfalse );
			if ( com_tokentype != TK_QUOTED )
			{
				// "drop" action can have empty message (defaults to "Banned.")
				if ( /*fop == FilterOp::Drop && */ ( com_tokentype == TK_NEWLINE || com_tokentype == TK_EOF || *v0 == '}' ) )
				{
					if ( com_tokentype == TK_NEWLINE || com_tokentype == TK_EOF )
						v0 = "";
					text = back; // restore backup
				}
				else
				{
					COM_ParseError( "unexpected '%s'", v0 );
					return nullptr;
				}
			}
			ch = new_node( v0, "0", fop, false ); // action node, p2 = "0", quoted = false
		}
		else
		{
			// save key
			Q_strncpyz( lvalue.data(), v0, SV_ArraySize(lvalue) );
			// expect operator or value
			v0 = COM_ParseComplex( &text, qfalse );
			// override default op
			if ( com_tokentype >= TK_EQ && com_tokentype <= TK_MATCH )
			{
				op = com_tokentype;
				v0 = COM_ParseComplex( &text, qfalse ); // get rvalue
			}
			else
			{
				if ( Q_stricmp( lvalue.data(), "date" ) == 0 )
					op = TK_LT; // default OP is LESS for dates
				else
					op = TK_EQ; // default OP is EQUAL for strings/integers
			}

			//  value must be sting or quoted string, `~` must be used with quoted strings only
			if ( (com_tokentype != TK_STRING && com_tokentype != TK_QUOTED) || (op == TK_MATCH && com_tokentype != TK_QUOTED ) )
			{
				COM_ParseError( "expecting value for key '%s' instead of '%s'", lvalue.data(), v0 );
				return nullptr;
			}

			switch ( op )
			{
				case TK_EQ:    fop = FilterOp::Eq;    break;
				case TK_NEQ:   fop = FilterOp::Neq;   break;
				case TK_LT:    fop = FilterOp::Lt;    break;
				case TK_LTE:   fop = FilterOp::Lte;   break;
				case TK_GT:    fop = FilterOp::Gt;    break;
				case TK_GTE:   fop = FilterOp::Gte;   break;
				case TK_MATCH: fop = FilterOp::Match; break;
				default:
					COM_ParseError( "bad operator #%i", op );
					return nullptr;
			}

			//Com_Printf( "%i: KEY %s <%i> %s\n", level, lvalue, fop, v0 ); // debug

			// allocate new filter node
			ch = new_node( lvalue.data(), v0, fop, com_tokentype == TK_QUOTED );
			if ( ch == nullptr )
				return nullptr;

			back = text;
			v0 = COM_ParseComplex( &text, qfalse ); // check current line
			if ( *v0 == '{' ) // open new section
			{
				text = parse_section( text, level + 1, &ch->child, true );
			}
			else if ( com_tokentype == TK_STRING ) // new key/action on the same line, open new section
			{
				text = parse_section( back, level + 1, &ch->child, false );
			} 
			else if ( com_tokentype == TK_NEWLINE || com_tokentype == TK_EOF )  // expect new section
			{
				v0 = COM_ParseComplex( &text, qtrue );
				if ( *v0 == '{' )
				{ 
					text = parse_section( text, level + 1, &ch->child, true );
				}
				else
				{
					COM_ParseError( "expecting new section/action node" );
					text = nullptr;
				}
			} // else parse new key/action
		}

		// update node pointers
		if ( curr == nullptr )
			*root = ch;
		else 
			curr->next = ch;

		curr = ch;

		if ( text == nullptr )
			break;
	}

	return text;
}


static bool parse_file( const char *filename )
{
	const char *text;
	char *data;
	qtime_t t;
	FILE *f;
	int size;
	
	// unconditionally release old filters
	free_nodes( nodes );
	nodes = nullptr;

	nodeCount = 0;
	tempCount = 0;

	if ( !filename || !*filename )
		return false;

	f = fopen( filename, "rb" );
	if ( f == nullptr )
		return false;
	const auto closeFile = SV_MakeScopeExit( [&]() { SV_CloseStdFile( f ); } );

	//Com_Printf( "...loading userinfo filters form '%s'\n", filename );

	fseek( f, 0, SEEK_END );
	size = ftell( f );
	fseek( f, 0, SEEK_SET );

	data = SV_ZMallocArray<char>( size + 1 );
	const auto freeData = SV_MakeScopeExit( [&]() { SV_ZFree( data ); } );
	if ( fread( data, size, 1, f ) != 1 )
	{
		return false;
	}

	data[ size ] = '\0';

	COM_BeginParseSession( filename );

	text = parse_section( data, 0, &nodes, true );

	if ( text == nullptr ) // error
	{
		free_nodes( nodes );
		nodes = nullptr;
	}

	if ( text == nullptr )
		return false;

	// initialize date string
	Com_RealTime( &t );
	Com_sprintf( filterDate.data(), SV_ArraySize(filterDate), "%04i-%02i-%02i %02i:%02i",
		t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
		t.tm_hour, t.tm_min );

	filterDateMsec = Sys_Milliseconds();

	return true;
}


static void SV_ReloadFilters( const char *filename, filter_node_t *new_node )
{
	static std::array<char, MAX_OSPATH * 3> loaded_name{};
	static fileOffset_t loaded_fsize;
	static fileTime_t loaded_ctime;
	static fileTime_t loaded_mtime;
	const char *ospath;

	if ( *filename && ( tempCount || new_node ) )
	{
		fileTime_t curr_ctime, curr_mtime;
		fileOffset_t curr_fsize;
		bool reload;
		bool dump;

		ospath = FS_BuildOSPath( FS_GetHomePath(), FS_GetCurrentGameDir(), filename );
		if ( strcmp( ospath, loaded_name.data() ) )
			reload = true;
		else if ( !Sys_GetFileStats( loaded_name.data(), &curr_fsize, &curr_mtime, &curr_ctime ) )
			reload = true;
		else if ( curr_fsize != loaded_fsize || curr_mtime != loaded_mtime || curr_ctime != loaded_ctime )
			reload = true;
		else
			reload = false;

		if ( reload )
		{
			//Com_Printf( "...reloading filter nodes from %s\n", ospath );
			if ( parse_file( ospath ) )
			{
				Q_strncpyz( loaded_name.data(), ospath, SV_ArraySize(loaded_name) );
				Sys_GetFileStats( loaded_name.data(), &loaded_fsize, &loaded_mtime, &loaded_ctime );
			}
		}

		dump = false;

		// add new nodes(s)
		if ( new_node )
		{
			clear_tags( nodes );
			// link new new node
			new_node->next = nodes;
			nodes = new_node;
			dump = true;
		}

		// tag expired nodes
		if ( tempCount )
		{
			expiredCount = 0;
			// find single expired nodes
			tag_expired( nodes );
			if ( expiredCount ) 
			{
				tag_parents( nodes );
				dump = true;
			}
		}

		if ( dump )
		{
			FILE *f;
			f = Sys_FOpen( ospath, "w" );
			if ( f ) 
			{
				const auto closeFile = SV_MakeScopeExit( [&]() { SV_CloseStdFile( f ); } );
				dump_nodes( nodes, 0, true, f ); // skip tagged
			}
		}
	}

	ospath = FS_BuildOSPath( FS_GetHomePath(), FS_GetCurrentGameDir(), filename );
	if ( *filename && strcmp( ospath, loaded_name.data() ) == 0 )
	{
		fileTime_t curr_ctime, curr_mtime;
		fileOffset_t curr_fsize;
		if ( Sys_GetFileStats( ospath, &curr_fsize, &curr_mtime, &curr_ctime ) )
		{
			if ( curr_fsize == loaded_fsize && curr_mtime == loaded_mtime && curr_ctime == loaded_ctime )
			{
				return; // filter file not changed
			}
		}
	}

	loaded_name[ 0 ] = '\0';

	if ( parse_file( ospath ) )
	{
		Com_Printf( "...%i filter nodes loaded from '%s'\n", nodeCount, filename );
		// save file metadata
		Q_strncpyz( loaded_name.data(), ospath, SV_ArraySize(loaded_name) );
		Sys_GetFileStats( loaded_name.data(), &loaded_fsize, &loaded_mtime, &loaded_ctime );
	}
}


void SV_LoadFilters( const char *filename )
{
	SV_ReloadFilters( filename, nullptr );
}


const char *SV_RunFilters( const char *userinfo, const netadr_t *addr )
{
	if ( addr->type <= NA_LOOPBACK ) // cannot kick host player/bot
		return "";

	Info_Tokenize( userinfo );

	filterName[0] = '\0';
	filterMessage[0] = '\0';
	filterCurrMsec = Sys_Milliseconds();

	if ( walk_nodes( nodes ) != 0 )
	{
		if ( filterMessage[0] )
			return filterMessage.data();
		else
			return "Banned.";
	}
	else
		return "";
}


static constexpr bool IsLeapYear( unsigned int year )
{
	return ( year % 4 == 0 && year % 100 != 0 ) || year % 400 == 0;
}

/* Add hours to specified date */
static void Q_AddTime( qtime_t *qtime, unsigned int n )
{
	auto md = MONTH_DAYS;
	unsigned int year, month, day, min, hour;

	year = qtime->tm_year + 1900;
	month = qtime->tm_mon;
	day = qtime->tm_mday;
	hour = qtime->tm_hour;
	min = qtime->tm_min;

	// add minutes
	n += min;  min  = n % 60; n -= min;  n /= 60; // hours

	// add hours
	n += hour; hour = n % 24; n -= hour; n /= 24; // days

	// add days
	if ( IsLeapYear( year ) )
		md[1] = 29;
	else
		md[1] = 28;

	// add days-months-years
	while ( true )
	{
		if ( day + n > md[month] )
		{
			n -= ( md[month] - day + 1 );
			month++;
			day = 1;
			if ( month > 11 )
			{
				year++;
				month = 0;
				if ( IsLeapYear( year ) )
					md[1] = 29;
				else
					md[1] = 28;
			}
		}
		else
		{
			day += n;
			break;
		}
	}

	qtime->tm_year = year - 1900;
	qtime->tm_mon = month;
	qtime->tm_mday = day;
	qtime->tm_hour = hour;
	qtime->tm_min = min;
}


/* Add months to specified date */
static void Q_AddDate( qtime_t *qtime, int n )
{
	auto md = MONTH_DAYS;
	unsigned int year, month, day, last;

	year = qtime->tm_year + 1900;
	month = qtime->tm_mon;
	day = qtime->tm_mday;

	last = (day == md[month]);

	if ( IsLeapYear( year ) )
		md[1] = 29;
	else
		md[1] = 28;

	n += month; month = n % 12;	n -= month;	n /= 12; year += n;

	if ( IsLeapYear( year ) )
		md[1] = 29;
	else
		md[1] = 28;

	if ( last )
		day = md[ month ];
	else
		if ( day > md[ month ] )
			day = md[ month ];

	qtime->tm_year = year - 1900;
	qtime->tm_mon = month;
	qtime->tm_mday = day;
}


/*
===============
SV_AddFilter_f
===============
*/
void SV_AddFilter_f( void )
{
	filter_node_t *node;
	client_t *cl;
	std::array<char, 4096> cmd{};
	std::array<char, MAX_CMD_LINE> buf{};
	std::array<char, 32> date{};
	const char *v, *s;
	const char *reason;
	qtime_t t;
	int i, n, keys;

	if ( !sv_filter->string[0] )
	{
		Com_Printf( "Filter system is not enabled.\n" );
		SV_ReloadFilters( "", nullptr );
		return;
	}

	if ( Cmd_Argc() < 2 )
	{
		Com_Printf( "Usage: %s <id> [key1] [key2] ... [keyN] [date +<duration[h|d|w|m]>|<date> ] [reason <text>]\nDefault key is \"ip\"\nDefault duration unit is minutes, h(ours), d(ays), w(eeks), m(onths) suffixes can also be specified.\n", Cmd_Argv( 0 ) );
		return;
	}

	cl = SV_GetPlayerByHandle();
	if ( cl == nullptr )
	{
		Com_Printf( "Unknown client '%s'\n", Cmd_Argv( 1 ) );
		return;
	}

	Com_RealTime( &t );
	keys = 0;
	reason = "";

	Info_Tokenize( cl->userinfo );

	// attach userinfo keys
	for ( i = 2; i < Cmd_Argc(); i++ )
	{
		v = Cmd_Argv( i );

		// special case: ban reason
		if ( Q_stricmp( v, "reason" ) == 0 )
		{
			if ( i >= Cmd_Argc() - 1 )
			{
				Com_Printf( S_COLOR_YELLOW "missing reason value\n" );
				return;
			}
			reason = Cmd_Argv( i + 1 );
			i++;
			continue;
		}

		// special case: duration (date) field
		if ( Q_stricmp( v, "date" ) == 0 )
		{
			if ( i >= Cmd_Argc() - 1 )
			{
				Com_Printf( S_COLOR_YELLOW "missing date value\n" );
				return;
			}
			i++;
			v = Cmd_Argv( i );
			if ( v[0] == '+' )
			{
				v++;
				if ( *v < '1' || *v > '9' )
				{
					Com_Printf( "expecting integer value for duration\n" );
					return;
				}
				n = 0;
				while ( *v >= '0' && *v <= '9' ) {
					n = n * 10 + ( *v++ - '0' );
				}
				switch ( *v ) {
					case '\0': Q_AddTime( &t, n ); break;
					case 'H': case 'h': Q_AddTime( &t, n * 60 ); break;
					case 'D': case 'd': Q_AddTime( &t, n * 24 * 60 ); break;
					case 'W': case 'w': Q_AddTime( &t, n * 24 * 7 * 60 ); break;
					case 'M': case 'm': Q_AddDate( &t, n ); break;
					default:
						Com_Printf( S_COLOR_YELLOW "unsupported date suffix '%c'\n", *v );
						return;
				}
				Com_sprintf( date.data(), SV_ArraySize(date), " date \"%04i-%02i-%02i %02i:%02i\"", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min );
			}
			else
			{
				Com_sprintf( date.data(), SV_ArraySize(date), " date \"%s\"", v );
			}
			continue;
		}

		s = Info_ValueForKeyToken( v );
		if ( *s == '\0' ) // skip empty keys
			continue;

		Com_sprintf( buf.data(), SV_ArraySize(buf), " %s \"%s\"", v, s );
		Q_strcat( cmd.data(), SV_ArraySize(cmd), buf.data() );
		keys++;
	}

	if ( !keys ) // add default key(s)
	{
		Com_sprintf( buf.data(), SV_ArraySize(buf), " ip \"%s\"", Info_ValueForKeyToken( "ip" ) );
		Q_strcat( cmd.data(), SV_ArraySize(cmd), buf.data() );
	}

	if ( date[0] )
		Q_strcat( cmd.data(), SV_ArraySize(cmd), date.data() );

	if ( *reason )
		Com_sprintf( buf.data(), SV_ArraySize(buf), " drop \"%s\"", reason );
	else
		Q_strncpyz( buf.data(), " drop", SV_ArraySize(buf) );

	Q_strcat( cmd.data(), SV_ArraySize(cmd), buf.data() );

	Com_DPrintf( "bancmd: `%s`\n", cmd.data() );

	node = nullptr;
	COM_BeginParseSession( "command" );
	s = parse_section( cmd.data(), 0, &node, true );
	if ( s == nullptr ) // syntax error
	{
		free_nodes( node );
		return;
	}

	if ( node && node->fop == FilterOp::Drop )
	{
		Com_Printf( S_COLOR_YELLOW "Standalone \"drop\" nodes is not allowed!\n" );
		free_nodes( node );
		return;
	}

	if ( node ) // should always success
	{
		SV_ReloadFilters( sv_filter->string, node );
		SV_DropClient( cl, *reason ? reason : "Banned." );
	}
}


/*
===============
SV_AddFilterCmd_f

Parses raw filter command string
===============
*/
void SV_AddFilterCmd_f( void ) 
{
	filter_node_t *node;
	const char *cmd, *s;

	if ( !sv_filter->string[0] ) 
	{
		Com_Printf( "Filter system is not enabled.\n" );
		SV_ReloadFilters( "", nullptr );
		return;
	}

	if ( Cmd_Argc() < 2 )
	{
		Com_Printf( "Usage: %s <filter format string>\n", Cmd_Argv( 0 ) );
		return;
	}

	cmd = Cmd_Cmd() + strlen( Cmd_Argv( 0 ) ) + 1;

	node = nullptr;
	COM_BeginParseSession( "command" );
	s = parse_section( cmd, 0, &node, true );
	if ( s == nullptr ) // syntax error
	{
		free_nodes( node );
		return;
	}

	if ( node && node->fop == FilterOp::Drop )
	{
		Com_Printf( S_COLOR_YELLOW "Standalone \"drop\" nodes is not allowed!\n" );
		free_nodes( node );
		return;
	}

	if ( node )
	{
		SV_ReloadFilters( sv_filter->string, node );
	}
}
