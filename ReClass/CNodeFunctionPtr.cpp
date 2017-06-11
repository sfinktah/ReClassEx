#include "stdafx.h"
#include "CNodeFunctionPtr.h"

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

CNodeFunctionPtr::CNodeFunctionPtr( )
	: m_pAssemblyWindow( NULL )
	, m_pParentWindow( NULL )
	, m_nLines( 0 )
	, m_nLongestLine( 0 )
	, m_iWidth( 0 )
	, m_iHeight( 0 )
	, m_bRedrawNeeded( FALSE )
{
	m_nodeType = nt_functionptr;
	m_strName = _T( "" );
}

CNodeFunctionPtr::CNodeFunctionPtr( CWnd* pParentWindow, ULONG_PTR Address )
	: CNodeFunctionPtr( )
{
	Initialize( pParentWindow, Address );
}

CNodeFunctionPtr::~CNodeFunctionPtr( )
{
	if (m_pAssemblyWindow != NULL)
	{
		m_pAssemblyWindow->Clear( );
		m_pAssemblyWindow->ShowWindow( SW_HIDE );
		
		delete m_pAssemblyWindow;
		m_pAssemblyWindow = NULL;
	}
}

void CNodeFunctionPtr::Update( const HotSpot& Spot )
{
	StandardUpdate( Spot );

	if (Spot.ID == 0)
	{
		// Re-read bytes at specified address
		DisassembleBytes( Spot.Address );
	}
}

NodeSize CNodeFunctionPtr::Draw( const ViewInfo& View, int x, int y )
{
	NodeSize drawnSize;
	int tx = 0;
	int ax = 0;

	if (m_bHidden)
		return DrawHidden( View, x, y );

	AddSelection( View, 0, y, g_FontHeight );
	AddDelete( View, x, y );
	AddTypeDrop( View, x, y );
	//AddAdd(View,x,y);

	tx = x + TXOFFSET;
	tx = AddIcon( View, tx, y, ICON_METHOD, -1, -1 );
	ax = tx;
	tx = AddAddressOffset( View, tx, y );

	if (m_pParentNode->GetType( ) != nt_vtable)
	{
		tx = AddText( View, tx, y, g_crType, HS_NONE, _T( "FunctionPtr" ) );
	}
	else
	{
		tx = AddText( View, tx, y, g_crFunction, HS_NONE, _T( "(%i)" ), m_Offset / sizeof( ULONG_PTR ) );
	}

	tx = AddIcon( View, tx, y, ICON_CAMERA, HS_EDIT, HS_CLICK );
	tx += g_FontWidth;

	if (m_strName.IsEmpty( ))
	{
		tx = AddText( View, tx, y, g_crName, HS_NAME, _T( "Function_%i" ), m_Offset / sizeof( ULONG_PTR ) );
	}
	else
	{
		tx = AddText( View, tx, y, g_crName, HS_NAME, _T( "%s" ), m_strName );
	}

	tx += g_FontWidth;

	if (m_nLines > 0)
		tx = AddOpenClose( View, tx, y );

	tx += g_FontWidth;

	tx = AddComment( View, tx, y );

	if (m_LevelsOpen[View.Level])
	{
		//for (size_t i = 0; i < m_Assembly.size( ); i++)
		//{
		//	y += g_FontHeight;
		//	AddText( View, ax, y, g_crHex, HS_EDIT, "%s", m_Assembly[i].GetBuffer( ) );
		//}

		y += g_FontHeight;

		if (m_pAssemblyWindow != NULL)
		{
			if (m_bRedrawNeeded)
			{
				m_pAssemblyWindow->MoveWindow( ax, y, m_iWidth, m_iHeight );
				m_pAssemblyWindow->ShowWindow( SW_SHOW );

				m_bRedrawNeeded = FALSE;
			}
			else
			{
				m_pAssemblyWindow->MoveWindow( ax, y, m_iWidth, m_iHeight );
			}

			y += m_iHeight;
		}
	}
	else
	{
		if (m_pAssemblyWindow != NULL)
		{
			m_pAssemblyWindow->ShowWindow( SW_HIDE );
			m_bRedrawNeeded = TRUE;
		}

		y += g_FontHeight;
	}

	drawnSize.x = tx;
	drawnSize.y = y;
	return drawnSize;
}

void CNodeFunctionPtr::Initialize( CWnd* pParentWindow, ULONG_PTR Address )
{
	if (m_pAssemblyWindow != NULL)
	{
		m_pAssemblyWindow->Clear( );
		m_pAssemblyWindow->ShowWindow( SW_HIDE );

		delete m_pAssemblyWindow;
		m_pAssemblyWindow = NULL;
	}

	m_pAssemblyWindow = new CScintillaEdit;

	m_pParentWindow = static_cast<CWnd*>(pParentWindow);

	m_pAssemblyWindow->Create( WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, CRect( 0, 0, 0, 0 ), m_pParentWindow, 0 );
	m_pAssemblyWindow->ShowWindow( SW_HIDE ); // Hide the window until we open the level
	//m_pAssemblyWindow->EnableWindow( FALSE ); // Disables the ability to scroll
	m_pAssemblyWindow->EnableScrollBarCtrl( SB_BOTH, FALSE );

	m_pAssemblyWindow->SetLexer( SCLEX_ASM );
	m_pAssemblyWindow->SetStyleBits( 5 );
	m_pAssemblyWindow->SetTabWidth( 2 );
	m_pAssemblyWindow->SetForeground( STYLE_DEFAULT, black );
	m_pAssemblyWindow->SetBackground( STYLE_DEFAULT, g_crBackground );
	m_pAssemblyWindow->SetSize( STYLE_DEFAULT, FONT_DEFAULT_SIZE );
	m_pAssemblyWindow->SetHorizontalScrollVisible( FALSE );
	m_pAssemblyWindow->SetVerticalScrollVisible( FALSE );
	m_pAssemblyWindow->SetFont( STYLE_DEFAULT, g_ViewFontName );

	m_pAssemblyWindow->SetAllStylesDefault( );

	// Set syntax colors
	for (int i = 0; s_rgbSyntaxAsm[i].iItem != -1; i++)
		m_pAssemblyWindow->SetForeground( s_rgbSyntaxAsm[i].iItem, s_rgbSyntaxAsm[i].rgb );

	m_pAssemblyWindow->SetMarginWidth( 0, 0 );
	m_pAssemblyWindow->SetMarginWidth( 1, 0 );

	// Finally, disassemble the bytes to get the memsize, height, and width
	DisassembleBytes( Address );
}

void CNodeFunctionPtr::DisassembleBytes( ULONG_PTR Address )
{
	UCHAR Code[2048] = { 0xCC }; // max function length
	std::uintptr_t VirtualAddress = Address;

	// Clear old disassembly info
	m_pAssemblyWindow->SetReadOnly( FALSE );
	m_pAssemblyWindow->Clear( );
	m_pAssemblyWindow->SetReadOnly( TRUE );

	m_Assembly.clear( );
	m_nLongestLine = 0;

	std::size_t longest_ins = 0u;
	std::size_t num_insn = 0u;

	// Read in process bytes
	if (
		ReClassReadMemory( (LPVOID)VirtualAddress, (LPVOID)&VirtualAddress, sizeof( std::uintptr_t ) ) == TRUE &&
		ReClassReadMemory( (LPVOID)VirtualAddress, (LPVOID)Code, 2048 ) == TRUE
		)
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

		auto insn = dis.Disasm(Code, 2048, VirtualAddress);

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
		}

		// Get rid of new line on last assembly instruction
		m_Assembly.back().Replace( "\r\n", "\0" );
	}
	else
	{
		m_Assembly.emplace_back( "ERROR: Could not read memory" );
	}

	// Get number of assembly lines
	m_nLines = (ULONG)m_Assembly.size( );

	// Clear any left over text
	m_pAssemblyWindow->Clear( );

	// Make the edit window temporarily editable
	m_pAssemblyWindow->SetReadOnly( FALSE );

	for (ULONG i = 0; i < m_nLines; i++)
	{
		ULONG nCurrentLineLength = 0;

		// Append text to window
		m_pAssemblyWindow->AppendText( m_Assembly[i].GetString( ) );

		// Calculate width from longest assembly instruction		
		nCurrentLineLength = m_pAssemblyWindow->LineLength( i );
		if (nCurrentLineLength > m_nLongestLine)
			m_nLongestLine = nCurrentLineLength;
	}

	// Back to read only
	m_pAssemblyWindow->SetReadOnly( TRUE );

	// Set caret at the beginning of documents
	m_pAssemblyWindow->SetSelection( 0, 0 );

	// Set the editor width and height
	m_iHeight = (m_pAssemblyWindow->PointYFromPosition( m_pAssemblyWindow->PositionFromLine( m_nLines ) ) - 
		m_pAssemblyWindow->PointYFromPosition( m_pAssemblyWindow->PositionFromLine( 0 ) )) + g_FontHeight;
	m_iWidth = (m_nLongestLine * g_FontWidth) + g_FontWidth;

	// Force a redraw
	m_bRedrawNeeded = TRUE;

}
