#include "stdafx.h"
#include "CNodeFunction.h"

static ScintillaColors s_rgbSyntaxAsm[] =
{
	{ SCE_ASM_COMMENT,		green },
	{ SCE_ASM_NUMBER,		darkyellow },
	{ SCE_ASM_STRING,		orange },
	{ SCE_ASM_REGISTER,		lightred },
	{ SCE_ASM_DIRECTIVE,	magenta },
	{ SCE_ASM_OPERATOR,		blue },
	{ SCE_ASM_DIRECTIVE,	purple },
	{ -1,					0 }
};

CNodeFunction::CNodeFunction( ) :
	m_pEdit( NULL ),
	m_nLines( 0 ),
	m_nLongestLine( 0 ),
	m_iWidth( 0 ),
	m_iHeight( 0 ),
	m_bRedrawNeeded( FALSE )
{
	m_nodeType = nt_function;
	m_strName = _T( "" );
	m_dwMemorySize = sizeof( ULONG_PTR );
}

CNodeFunction::~CNodeFunction( )
{
	if (m_pEdit != NULL)
	{
		m_pEdit->Clear( );
		m_pEdit->ShowWindow( SW_HIDE );

		delete m_pEdit;
		m_pEdit = NULL;
	}
}

void CNodeFunction::Update( const HotSpot& Spot )
{
	StandardUpdate( Spot );

	if (Spot.ID == 0)
	{
		// Re-read bytes at specified address
		DisassembleBytes( Spot.Address );
	}
}

NodeSize CNodeFunction::Draw( const ViewInfo& View, int x, int y )
{
	if (m_bHidden)
		return DrawHidden( View, x, y );

	NodeSize drawnSize;
	AddSelection( View, 0, y, g_FontHeight );
	AddDelete( View, x, y );
	AddTypeDrop( View, x, y );
	//AddAdd(View,x,y);

	int tx = x + TXOFFSET;
	int ax = 0;

	tx = AddIcon( View, tx, y, ICON_METHOD, -1, -1 );
	ax = tx;
	tx = AddAddressOffset( View, tx, y );

	tx = AddText( View, tx, y, g_crType, HS_NONE, _T( "Function" ) );

	tx = AddIcon( View, tx, y, ICON_CAMERA, HS_EDIT, HS_CLICK );
	tx += g_FontWidth;

	tx = AddText( View, tx, y, g_crName, HS_NAME, _T( "%s" ), m_strName );
	tx += g_FontWidth;

	if (m_nLines > 0)
		tx = AddOpenClose( View, tx, y );

	tx += g_FontWidth;

	tx = AddComment( View, tx, y );

	if (m_LevelsOpen[View.Level])
	{
		y += g_FontHeight;

		if (m_bRedrawNeeded)
		{	
			m_pEdit->MoveWindow( ax, y, m_iWidth, m_iHeight );
			m_pEdit->ShowWindow( SW_SHOW );

			m_bRedrawNeeded = FALSE;
		}
		else
		{
			m_pEdit->MoveWindow( ax, y, m_iWidth, m_iHeight );
		}

		y += m_iHeight;
	}
	else
	{
		m_pEdit->ShowWindow( SW_HIDE );
		m_bRedrawNeeded = TRUE;

		y += g_FontHeight;
	}

	drawnSize.x = tx;
	drawnSize.y = y;
	return drawnSize;
}

void CNodeFunction::Initialize( CClassView* pChild, ULONG_PTR Address )
{
	if (m_pEdit != NULL)
	{
		m_pEdit->Clear( );
		m_pEdit->ShowWindow( SW_HIDE );

		delete m_pEdit;
		m_pEdit = NULL;
	}

	m_pEdit = new CScintillaEdit;

	m_pEdit->Create( WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, CRect( 0, 0, 0, 0 ), (CWnd*)pChild, 0 );

	m_pEdit->ShowWindow( SW_HIDE ); // Hide the window until we open the level
	//m_pEdit->EnableWindow( FALSE ); // Disables the ability to scroll
	m_pEdit->EnableScrollBarCtrl( SB_BOTH, FALSE );

	m_pEdit->SetLexer( SCLEX_ASM );
	m_pEdit->SetStyleBits( 5 );
	m_pEdit->SetTabWidth( 2 );
	m_pEdit->SetForeground( STYLE_DEFAULT, black );
	m_pEdit->SetBackground( STYLE_DEFAULT, g_crBackground );
	m_pEdit->SetSize( STYLE_DEFAULT, FONT_DEFAULT_SIZE );
	m_pEdit->SetHorizontalScrollVisible( FALSE );
	m_pEdit->SetVerticalScrollVisible( FALSE );
	m_pEdit->SetFont( STYLE_DEFAULT, g_ViewFontName );

	m_pEdit->SetAllStylesDefault( );

	// Set syntax colors
	for (int i = 0; s_rgbSyntaxAsm[i].iItem != -1; i++)
		m_pEdit->SetForeground( s_rgbSyntaxAsm[i].iItem, s_rgbSyntaxAsm[i].rgb );

	m_pEdit->SetMarginWidth( 0, 0 );
	m_pEdit->SetMarginWidth( 1, 0 );

	// Finally, disassemble the bytes to get the memsize, height, and width
	DisassembleBytes( Address );
}

void CNodeFunction::DisassembleBytes( ULONG_PTR Address )
{
	ULONG_PTR StartAddress = Address;
	UCHAR Code[2048] = { 0xCC }; // set max function size to 2048 bytes
	//UIntPtr EndCode = (UIntPtr)(Code + 2048);
	
	// Clear old disassembly info
	if (m_pEdit)
	{
		m_pEdit->SetReadOnly( FALSE );
		m_pEdit->Clear( );
		m_pEdit->SetReadOnly( TRUE );
	}
	m_Assembly.clear( );
	m_dwMemorySize = 0;
	m_nLongestLine = 0;

	std::size_t longest_ins = 0u;
	std::size_t num_insn = 0u;

	// Read in process bytes
	if (ReClassReadMemory( (LPVOID)StartAddress, (LPVOID)Code, 2048 ) == TRUE)
	{
#ifdef _WIN64
		CX86Disasm64 dis;
#else
		CX86Disasm86 dis;
#endif

		if (dis.GetError())
			return;

		// set how deep should capstone reverse instruction
		dis.SetDetail(cs_opt_value::CS_OPT_ON);

		// set syntax for output disasembly string
		dis.SetSyntax(cs_opt_value::CS_OPT_SYNTAX_INTEL);

		auto insn = dis.Disasm(Code, 2048, StartAddress);

		// check if disassembling succesfull
		if (!insn)
			return;

		// preprocess disassembly
		for (; num_insn < insn->Count && num_insn < 2048; ++num_insn) {

			auto &ins = insn->Instructions(num_insn);

			for (std::size_t j = 0; j < ins->size; ++j) {
				if (ins->detail->groups[j] == cs_group_type::CS_GRP_INT) // int3 usually marks the end of a function
					goto end;
			}

			longest_ins = max(longest_ins, ins->size);
			continue;
		end:
			break;
		}

		longest_ins *= 3; // 3 characters per byte

		// print disassembly
		for (size_t i = 0; i < num_insn; i++) {

			auto &ins = insn->Instructions(i);

			CHAR str_inst[256]{};
			CHAR str_bytes[128]{};

			for (std::size_t i = 0; i < ins->size; ++i) {
				sprintf_s(str_bytes + (i * 3), 4, "%02X ", ins->bytes[i]);
			}

			int len = sprintf(str_inst, "%IX\t%-*s\t%s\t%s\r\n",
				static_cast<unsigned int>(ins->address),
				longest_ins,
				str_bytes,
				ins->mnemonic,
				ins->op_str);

			m_Assembly.emplace_back(str_inst);
			m_dwMemorySize += ins->size;
		}

		// Get rid of new line on last assembly instruction
		m_Assembly.back().Replace( "\r\n", "\0" );
	}
	else
	{
		m_Assembly.emplace_back( "ERROR: Could not read memory" );
		m_dwMemorySize = sizeof( void* );
	}

	// Get number of assembly lines
	m_nLines = (ULONG)m_Assembly.size( );

	// Clear any left over text
	m_pEdit->Clear( );

	// Make the edit window temporarily editable
	m_pEdit->SetReadOnly( FALSE );

	for (ULONG i = 0; i < m_nLines; i++)
	{
		ULONG nCurrentLineLength = 0;

		// Append text to window
		m_pEdit->AppendText( m_Assembly[i].GetString( ) );

		// Calculate width from longest assembly instruction		
		nCurrentLineLength = m_pEdit->LineLength( i );
		if (nCurrentLineLength > m_nLongestLine)
			m_nLongestLine = nCurrentLineLength;
	}

	// Back to read only
	m_pEdit->SetReadOnly( TRUE );

	// Set caret at the beginning of documents
	m_pEdit->SetSelection( 0, 0 );

	// Set the editor width and height
	m_iHeight = (m_pEdit->PointYFromPosition( m_pEdit->PositionFromLine( m_nLines ) ) - m_pEdit->PointYFromPosition( m_pEdit->PositionFromLine( 0 ) )) + g_FontHeight;
	m_iWidth = (m_nLongestLine * g_FontWidth) + g_FontWidth;

	// Force a redraw
	m_bRedrawNeeded = TRUE;
}

