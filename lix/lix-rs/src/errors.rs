use std::{
    any::TypeId,
    fmt::{Debug, Display},
};

use either::Either::{self, Left, Right};
use rootcause::{
    markers::{Dynamic, SendSync},
    report_attachments::ReportAttachments,
    report_collection::ReportCollection,
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

mod seal {
    pub trait Seal {}
}

/// turns [`Either`]s of reports into a report of [`Either`].
pub trait FactorReport: seal::Seal {
    type Factored;

    /// factors [`Either<Report<A>, Report<B>>`] into [`Report<Either<A, B>>`] or [`Report`].
    ///
    /// if neither `A` nor `B` are [`Dynamic`] then [`Either<A, B>`] context will result,
    /// otherwise [`Dynamic`] context (i.e., a simple [`Report`]). this can be applied to
    /// both plain [`Either`]s as well as to [`Result`]s containing them as error values.
    fn factor_report(self) -> Self::Factored;
}

impl<L> seal::Seal for Either<Report<L>, Report> {}
impl<L> FactorReport for Either<Report<L>, Report> {
    type Factored = Report<Dynamic>;

    fn factor_report(self) -> Self::Factored {
        self.either(Into::into, Into::into)
    }
}

impl<R> seal::Seal for Either<Report, Report<R>> {}
impl<R> FactorReport for Either<Report, Report<R>> {
    type Factored = Report<Dynamic>;

    fn factor_report(self) -> Self::Factored {
        self.either(Into::into, Into::into)
    }
}

impl<L, R> seal::Seal for Either<Report<L>, Report<R>>
where
    L: Send + Sync + Display + Debug + 'static,
    R: Send + Sync + Display + Debug + 'static,
{
}
impl<L, R> FactorReport for Either<Report<L>, Report<R>>
where
    L: Send + Sync + Display + Debug + 'static,
    R: Send + Sync + Display + Debug + 'static,
{
    type Factored = Report<Either<L, R>>;

    fn factor_report(self) -> Self::Factored {
        self.either(|e| e.context_transform(Left), |e| e.context_transform(Right))
    }
}

impl<R, A: ?Sized, B: ?Sized> seal::Seal for Result<R, Either<Report<A>, Report<B>>> where
    Either<Report<A>, Report<B>>: FactorReport
{
}
impl<R, A: ?Sized, B: ?Sized> FactorReport for Result<R, Either<Report<A>, Report<B>>>
where
    Either<Report<A>, Report<B>>: FactorReport,
{
    type Factored = Result<R, <Either<Report<A>, Report<B>> as FactorReport>::Factored>;

    fn factor_report(self) -> Self::Factored {
        self.map_err(|e| e.factor_report())
    }
}

#[deprecated(note = "for c++ compatibility only, use Report::context instead")]
pub(crate) trait DynamicReportExt: seal::Seal {
    fn context_then_dynamic(self, s: &str) -> Report;
}

impl<C: ?Sized> seal::Seal for Report<C> {}

#[allow(deprecated)]
impl<C: ?Sized> DynamicReportExt for Report<C> {
    fn context_then_dynamic(self, s: &str) -> Report {
        let mut result = self.context(s.to_string()).into_dynamic();
        // filter location information from the report we just created. that info would
        // always point here and thus not be useful, and getting good line number infos
        // out of c++ into this function is (surprise surprise!) unreasonably difficult
        let mut filtered_attachments = ReportAttachments::new();
        while let Some(a) = result.attachments_mut().pop().filter(|a| {
            a.inner_type_id() != TypeId::of::<rootcause::hooks::builtin_hooks::location::Location>()
        }) {
            filtered_attachments.push(a);
        }
        while let Some(a) = filtered_attachments.pop() {
            result.attachments_mut().push(a);
        }
        result
    }
}
