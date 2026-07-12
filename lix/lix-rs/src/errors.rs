use rootcause::{
    markers::SendSync, report_attachments::ReportAttachments, report_collection::ReportCollection,
    Report,
};

pub(crate) fn report_from_string_unhooked(s: String) -> Report {
    let r: Report<_, _, SendSync> = Report::from_parts_unhooked::<rootcause::handlers::Display>(
        s,
        ReportCollection::new(),
        ReportAttachments::new(),
    );
    r.into_dynamic()
}

/// This formats a list of (C++) exception messages as a rootcause's report that can be thrown back
/// on the C++ side.
pub fn format_exception_messages_as_rootcause_report(
    global_context: String,
    messages: Vec<String>,
) -> rootcause::Report {
    let mut report = report_from_string_unhooked(global_context);

    report
        .children_mut()
        .extend(messages.into_iter().map(report_from_string_unhooked));

    report
}
